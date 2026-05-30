// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Database.h"

#include "PreparedQuery.h"
#include "Query.h"
#include "Transaction.h"

static bool isUserCancellationError(const PGException &error) {
    return error.sqlstate == "57014" || std::string(error.what()).find("canceling statement due to user request") != std::string::npos;
}

std::shared_ptr<Database> Database::createDatabase(const std::string &host, const std::string &username,
                                                   const std::string &password, const std::string &database,
                                                   unsigned int port, const std::string &unixSocket) {
    return std::shared_ptr<Database>(new Database(host, username, password, database, port, unixSocket));
}

std::shared_ptr<Database> Database::createDatabaseFromConnectionString(const std::string &connectionString) {
    return std::shared_ptr<Database>(new Database(connectionString));
}

std::shared_ptr<Database> Database::createDatabaseFromOptions(const std::vector<std::pair<std::string, std::string>> &options) {
    return std::shared_ptr<Database>(new Database(options));
}

Database::Database(std::string host, std::string username, std::string password, std::string database,
                   unsigned int port, std::string unixSocket)
    : m_config(ConnectionConfig::positional(std::move(host), std::move(username), std::move(password),
                                            std::move(database), port, std::move(unixSocket))) {}

Database::Database(std::string connectionString)
    : m_config(ConnectionConfig::rawConnectionString(std::move(connectionString))) {}

Database::Database(std::vector<std::pair<std::string, std::string>> options)
    : m_config(ConnectionConfig::options(std::move(options))) {}

Database::~Database() {
    shutdown();
    if (m_thread.joinable()) m_thread.join();
}

void Database::enqueueQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    if (m_shuttingDown || m_worker.isClosed()) {
        data->setStatus(QUERY_ABORTED);
        data->setFinished(true);
        throw PGException("Database is disconnected.");
    }
    m_worker.enqueue(query, data);
}

bool Database::swapQueryToFront(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    return m_worker.swapToFront(query, data);
}

QueryAbortResult Database::abortQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    QueryAbortResult result;
    bool wasRemoved = m_worker.removeQueued(query, data);
    if (wasRemoved) {
        data->setStatus(QUERY_ABORTED);
        data->setFinished(true);
        result.completed.emplace_back(query, data);
        result.requested = true;
        result.requestedCount = 1;
        return result;
    }

    if (data->getStatus() == QUERY_WAITING) {
        data->setStatus(QUERY_ABORTED);
        result.requested = true;
        result.requestedCount = 1;
        return result;
    }

    if (data->getStatus() == QUERY_RUNNING && cancelRunningQuery(data)) {
        result.requested = true;
        result.requestedCount = 1;
    }
    return result;
}

bool Database::cancelRunningQuery(const std::shared_ptr<IQueryData> &data) {
    return m_activeQuery.cancel(data);
}

void Database::addActiveQueryAlias(const std::shared_ptr<IQueryData> &data) {
    m_activeQuery.addAlias(data);
}

void Database::removeActiveQueryAlias(const std::shared_ptr<IQueryData> &data) {
    m_activeQuery.removeAlias(data);
}

std::shared_ptr<Query> Database::query(const std::string &query) { return Query::create(shared_from_this(), query); }
std::shared_ptr<PreparedQuery> Database::prepare(const std::string &query) { return PreparedQuery::create(shared_from_this(), query); }
std::shared_ptr<Transaction> Database::transaction() { return Transaction::create(shared_from_this()); }

void Database::connect() {
    bool expected = false;
    if (m_status != DATABASE_NOT_CONNECTED || !startedConnecting.compare_exchange_strong(expected, true)) {
        throw PGException("Database already connected.");
    }
    m_canWait = true;
    m_status = DATABASE_CONNECTING;
    m_thread = std::thread(&Database::connectRun, this);
}

void Database::wait() {
    if (!startedConnecting) throw PGException("Tried to wait for database connection before connect().");
    std::unique_lock<std::mutex> lock(m_connectMutex);
    while (!m_connectionDone) m_connectWakeupVariable.wait(lock);
}

void Database::disconnect(bool waitForThread) {
    if (!startedConnecting) return;
    shutdown();
    if (waitForThread && m_thread.joinable()) m_thread.join();
}

void Database::shutdown() {
    bool wasAlreadyShuttingDown = m_shuttingDown.exchange(true);
    if (wasAlreadyShuttingDown) return;
    m_status = DATABASE_NOT_CONNECTED;

    std::shared_ptr<IQueryData> activeData = m_activeQuery.currentData();
    if (activeData) cancelRunningQuery(activeData);
    auto inFlightData = currentInFlightData();
    if (inFlightData && inFlightData->getStatus() == QUERY_WAITING) inFlightData->setStatus(QUERY_ABORTED);

    auto aborted = m_worker.abortQueuedAndClose();
    for (const auto &pair : aborted.completed) {
        m_worker.finish(pair);
    }
}

bool Database::ping() {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    bool success = m_session.ping();
    if (!success && m_status == DATABASE_CONNECTED) {
        m_status = DATABASE_NOT_CONNECTED;
        if (shouldAutoReconnect) success = attemptReconnect();
    }
    return success;
}

std::string Database::escape(const std::string &str) {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return m_session.escape(str);
}

bool Database::setCharacterSet(const std::string &characterSet) {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return m_session.setCharacterSet(characterSet);
}

QueryAbortResult Database::abortAllQueries() {
    QueryAbortResult result = m_worker.abortQueued();

    auto inFlightData = currentInFlightData();
    if (inFlightData && inFlightData->getStatus() == QUERY_WAITING) {
        inFlightData->setStatus(QUERY_ABORTED);
        result.requested = true;
        result.requestedCount++;
    }

    std::shared_ptr<IQueryData> activeData = m_activeQuery.currentData();
    if (activeData && cancelRunningQuery(activeData)) {
        result.requested = true;
        result.requestedCount++;
    }
    return result;
}

DatabaseStatus Database::status() const { return m_status; }
unsigned int Database::serverVersion() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get server version when client is not connected to server yet!");
    }
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return m_session.serverVersion();
}

std::string Database::serverInfo() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get server info when client is not connected to server yet!");
    }
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return m_session.serverInfo();
}

std::string Database::hostInfo() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get host info when client is not connected to server yet!");
    }
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return m_session.hostInfo();
}
size_t Database::queueSize() { return m_worker.queueSize(); }
bool Database::wasDisconnected() { return disconnected; }

std::string Database::connectionError() {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return m_session.error();
}

void Database::setAutoReconnect(bool autoReconnect) { shouldAutoReconnect = autoReconnect; }
void Database::setMultiStatements(bool enabled) { m_multiStatements = enabled; }
bool Database::multiStatementsEnabled() const { return m_multiStatements; }
void Database::setConnectTimeout(unsigned int timeout) {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    m_config.setConnectTimeout(timeout);
}
void Database::setSSLMode(SSLMode mode) {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    m_config.setSSLMode(mode);
}

void Database::setSSLSettings(const SSLSettings &settings) {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    m_config.setSSLSettings(settings);
}

void Database::connectRun() {
    {
        std::lock_guard<std::mutex> lock(m_connectMutex);
        if (!attemptConnection()) {
            m_success = false;
            m_connectionDone = true;
            m_status = DATABASE_CONNECTION_FAILED;
            m_connectWakeupVariable.notify_one();
            disconnected = false;
            m_canWait = false;
            m_worker.completeQueuedWithErrorAndClose(m_session.error().empty() ? "Connection to database failed" : m_session.error());
            return;
        }
        m_success = true;
        m_connectionDone = true;
        m_status = DATABASE_CONNECTED;
        m_connectWakeupVariable.notify_one();
    }
    run();
    {
        std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
        m_session.reset();
    }
    m_canWait = false;
    disconnected = true;
    if (m_status == DATABASE_CONNECTED) m_status = DATABASE_NOT_CONNECTED;
}

bool Database::attemptConnection() {
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    return attemptConnectionUnlocked();
}

bool Database::attemptConnectionUnlocked() {
    return m_session.connect(m_config);
}

bool Database::attemptReconnect() {
    if (m_shuttingDown) return false;
    std::lock_guard<std::recursive_mutex> lock(m_queryMutex);
    m_activeQuery.clear();
    m_status = DATABASE_NOT_CONNECTED;
    m_session.reset();
    bool success = attemptConnectionUnlocked();
    m_status = success ? DATABASE_CONNECTED : DATABASE_CONNECTION_FAILED;
    m_reconnectLog.add(success, m_session.error());
    return success;
}

std::deque<std::pair<bool, std::string>> Database::takeReconnectEvents() {
    return m_reconnectLog.take();
}

void Database::runQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    try {
        if (!m_session.isOpen()) {
            if (!shouldAutoReconnect || !attemptReconnect()) throw PGConnectionException(m_session.error());
        }
        ActiveQueryState::Guard activeQuery(m_activeQuery, data, m_session.connection());
        query->executeStatement(*this, m_session.connection(), data);
        data->setCancellationRequested(false);
        data->setResultStatus(QUERY_SUCCESS);
    } catch (const PGConnectionException &error) {
        if (data->isCancellationRequested() && isUserCancellationError(error)) {
            data->setStatus(QUERY_ABORTED);
            data->setResultStatus(QUERY_NONE);
            return;
        }
        data->setCancellationRequested(false);
        if (shouldAutoReconnect && isRetriableError(error)) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        if (data->getError().empty()) data->setError(error.what());
    } catch (const PGException &error) {
        if (data->isCancellationRequested() && isUserCancellationError(error)) {
            data->setStatus(QUERY_ABORTED);
            data->setResultStatus(QUERY_NONE);
            return;
        }
        data->setCancellationRequested(false);
        data->setResultStatus(QUERY_ERROR);
        if (data->getError().empty()) data->setError(error.what());
    } catch (const std::exception &error) {
        data->setCancellationRequested(false);
        data->setResultStatus(QUERY_ERROR);
        if (data->getError().empty()) data->setError(error.what());
    }
}

void Database::run() {
    while (true) {
        std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>> pair;
        if (!m_worker.takeNext(pair)) return;
        setInFlight(pair.first, pair.second);
        auto data = pair.second;
        if (data->getStatus() == QUERY_ABORTED) {
            m_worker.finish(pair);
            data->setFinished(true);
            clearInFlight(data);
            continue;
        }
        {
            std::unique_lock<std::recursive_mutex> lock(m_queryMutex);
            if (m_shuttingDown || data->getStatus() == QUERY_ABORTED) {
                data->setStatus(QUERY_ABORTED);
                m_worker.finish(pair);
                data->setFinished(true);
                clearInFlight(data);
                continue;
            }
            data->setStatus(QUERY_RUNNING);
            runQuery(pair.first, data);
            if (data->getStatus() != QUERY_ABORTED) {
                data->setStatus(QUERY_COMPLETE);
            }
        }
        m_worker.finish(pair);
        data->setFinished(true);
        clearInFlight(data);
    }
}

void Database::waitForQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    if (data->isFinished()) return;
    if (!m_canWait) {
        if (data->getStatus() == QUERY_WAITING &&
            m_worker.completeQueuedWithError(query, data, "Can not wait on query, database is not connected or connection failed.")) {
            return;
        }
        completeQueryWithError(query, data, "Can not wait on query, database is not connected or connection failed.");
        return;
    }
    data->waitUntilFinished();
}

void Database::completeQueryWithError(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data,
                                 const std::string &reason) {
    if (data->isFinished()) return;
    data->setError(reason);
    data->setResultStatus(QUERY_ERROR);
    data->setStatus(QUERY_COMPLETE);
    data->setFinished(true);
    m_worker.finish(std::make_pair(query, data));
}

void Database::setInFlight(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    std::lock_guard<std::mutex> lock(m_inFlightMutex);
    m_inFlightQuery = query;
    m_inFlightData = data;
}

void Database::clearInFlight(const std::shared_ptr<IQueryData> &data) {
    std::lock_guard<std::mutex> lock(m_inFlightMutex);
    if (m_inFlightData == data) {
        m_inFlightQuery.reset();
        m_inFlightData.reset();
    }
}

std::shared_ptr<IQueryData> Database::currentInFlightData() {
    std::lock_guard<std::mutex> lock(m_inFlightMutex);
    return m_inFlightData;
}

bool Database::isRetriableError(const PGConnectionException &error) {
    return (error.sqlstate.size() >= 2 && error.sqlstate.substr(0, 2) == "08" && error.sqlstate != "08007") ||
           error.sqlstate.empty();
}
