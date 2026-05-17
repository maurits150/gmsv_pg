// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Database.h"

#include "PreparedQuery.h"
#include "Query.h"
#include "Transaction.h"

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
    if (m_shuttingDown) {
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

std::shared_ptr<Query> Database::query(const std::string &query) { return Query::create(shared_from_this(), query); }
std::shared_ptr<PreparedQuery> Database::prepare(const std::string &query) { return PreparedQuery::create(shared_from_this(), query); }
std::shared_ptr<Transaction> Database::transaction() { return Transaction::create(shared_from_this()); }

void Database::connect() {
    if (m_status != DATABASE_NOT_CONNECTED || startedConnecting) throw PGException("Database already connected.");
    m_canWait = true;
    startedConnecting = true;
    m_status = DATABASE_CONNECTING;
    m_thread = std::thread(&Database::connectRun, this);
}

void Database::wait() {
    if (!startedConnecting) throw PGException("Tried to wait for database connection before connect().");
    std::unique_lock<std::mutex> lock(m_connectMutex);
    while (!m_connectionDone) m_connectWakeupVariable.wait(lock);
}

void Database::disconnect(bool waitForThread) {
    shutdown();
    if (waitForThread && m_thread.joinable()) m_thread.join();
}

void Database::shutdown() {
    bool wasAlreadyShuttingDown = m_shuttingDown.exchange(true);
    if (wasAlreadyShuttingDown) return;

    std::shared_ptr<IQueryData> activeData = m_activeQuery.currentData();
    if (activeData) cancelRunningQuery(activeData);

    m_worker.abortQueued();
    m_worker.close();
}

bool Database::ping() {
    std::lock_guard<std::mutex> lock(m_queryMutex);
    return m_session.ping();
}

std::string Database::escape(const std::string &str) {
    std::lock_guard<std::mutex> lock(m_queryMutex);
    return m_session.escape(str);
}

bool Database::setCharacterSet(const std::string &characterSet) {
    std::lock_guard<std::mutex> lock(m_queryMutex);
    return m_session.setCharacterSet(characterSet);
}

QueryAbortResult Database::abortAllQueries() {
    QueryAbortResult result = m_worker.abortQueued();

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
    return m_session.serverVersion();
}

std::string Database::serverInfo() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get server info when client is not connected to server yet!");
    }
    return m_session.serverInfo();
}

std::string Database::hostInfo() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get host info when client is not connected to server yet!");
    }
    return m_session.hostInfo();
}
size_t Database::queueSize() { return m_worker.queueSize(); }
bool Database::wasDisconnected() { return disconnected; }

void Database::setAutoReconnect(bool autoReconnect) { shouldAutoReconnect = autoReconnect; }
void Database::setConnectTimeout(unsigned int timeout) { m_config.setConnectTimeout(timeout); }
void Database::setSSLMode(SSLMode mode) { m_config.setSSLMode(mode); }

void Database::setSSLSettings(const SSLSettings &settings) {
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
            disconnected = true;
            m_canWait = false;
            m_worker.completeQueuedWithError(m_session.error().empty() ? "Connection to database failed" : m_session.error());
            m_worker.close();
            return;
        }
        m_success = true;
        m_connectionDone = true;
        m_status = DATABASE_CONNECTED;
        m_connectWakeupVariable.notify_one();
    }
    run();
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
        m_session.reset();
    }
    m_canWait = false;
    disconnected = true;
    if (m_status == DATABASE_CONNECTED) m_status = DATABASE_NOT_CONNECTED;
}

bool Database::attemptConnection() {
    return m_session.connect(m_config);
}

bool Database::attemptReconnect() {
    if (m_shuttingDown) return false;
    m_session.reset();
    bool success = attemptConnection();
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
        ActiveQueryState::Guard activeQuery(m_activeQuery, data, m_session.connectionPtr());
        query->executeStatement(*this, m_session.connection(), data);
        data->setCancellationRequested(false);
        data->setResultStatus(QUERY_SUCCESS);
    } catch (const pqxx::broken_connection &error) {
        if (data->isCancellationRequested()) {
            data->setStatus(QUERY_ABORTED);
            data->setResultStatus(QUERY_NONE);
            return;
        }
        if (shouldAutoReconnect) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    } catch (const pqxx::sql_error &error) {
        if (data->isCancellationRequested()) {
            data->setStatus(QUERY_ABORTED);
            data->setResultStatus(QUERY_NONE);
            return;
        }
        PGConnectionException pgError(error.what(), error.sqlstate());
        if (shouldAutoReconnect && isRetriableError(pgError)) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    } catch (const PGConnectionException &error) {
        if (data->isCancellationRequested()) {
            data->setStatus(QUERY_ABORTED);
            data->setResultStatus(QUERY_NONE);
            return;
        }
        if (shouldAutoReconnect && isRetriableError(error)) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    } catch (const std::exception &error) {
        if (data->isCancellationRequested()) {
            data->setStatus(QUERY_ABORTED);
            data->setResultStatus(QUERY_NONE);
            return;
        }
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    }
}

void Database::run() {
    while (true) {
        std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>> pair;
        if (!m_worker.takeNext(pair)) return;
        auto data = pair.second;
        if (data->getStatus() == QUERY_ABORTED) {
            m_worker.finish(pair);
            data->setFinished(true);
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(m_queryMutex);
            if (data->getStatus() == QUERY_ABORTED) {
                m_worker.finish(pair);
                data->setFinished(true);
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
    }
}

void Database::waitForQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    if (!m_canWait) {
        completeQueryWithError(query, data, "Can not wait on query, database is not connected or connection failed.");
        return;
    }
    if (data->isFinished()) return;
    data->waitUntilFinished();
}

void Database::completeQueryWithError(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data,
                                const std::string &reason) {
    data->setError(reason);
    data->setResultStatus(QUERY_ERROR);
    data->setStatus(QUERY_COMPLETE);
    data->setFinished(true);
    m_worker.finish(std::make_pair(query, data));
}

bool Database::isRetriableError(const PGConnectionException &error) {
    return (error.sqlstate.size() >= 2 && error.sqlstate.substr(0, 2) == "08" && error.sqlstate != "08007") ||
           error.sqlstate.empty();
}
