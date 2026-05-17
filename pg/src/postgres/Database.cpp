// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Database.h"

#include <sstream>

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
    : database(std::move(database)), host(std::move(host)), username(std::move(username)),
      password(std::move(password)), unixSocket(std::move(unixSocket)), port(port) {}

Database::Database(std::string connectionString)
    : port(0), useRawConnectionString(true), rawConnectionString(std::move(connectionString)) {}

Database::Database(std::vector<std::pair<std::string, std::string>> options)
    : port(0), optionTable(std::move(options)) {}

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
    data->setStatus(QUERY_WAITING);
    if (!queryQueue.put(std::make_pair(query, data))) {
        data->setStatus(QUERY_ABORTED);
        data->setFinished(true);
        throw PGException("Database is disconnected.");
    }
}

bool Database::swapQueryToFront(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    return queryQueue.swapToFrontIf([&](std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>> pair) {
        return pair.first == query && pair.second == data;
    });
}

bool Database::cancelRunningQuery(const std::shared_ptr<IQueryData> &data) {
    std::lock_guard<std::mutex> lock(m_activeQueryMutex);
    if (m_activeQueryData != data || m_activeConnection == nullptr) return false;
    try {
        data->setStatus(QUERY_ABORTED);
        m_activeConnection->cancel_query();
        return true;
    } catch (const std::exception &) {
        data->setStatus(QUERY_RUNNING);
        return false;
    }
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

    auto queuedQueries = queryQueue.clear();
    for (auto &pair : queuedQueries) {
        if (!pair.second) continue;
        pair.second->setStatus(QUERY_ABORTED);
        pair.second->setFinished(true);
    }
    queryQueue.close();
}

bool Database::ping() {
    std::lock_guard<std::mutex> lock(m_queryMutex);
    if (!m_connection || !m_connection->is_open()) return false;
    pqxx::work tx(*m_connection);
    tx.exec("SELECT 1");
    tx.commit();
    return true;
}

std::string Database::escape(const std::string &str) {
    std::lock_guard<std::mutex> lock(m_queryMutex);
    if (!m_connection || !m_connection->is_open()) throw PGException("Cannot escape using database that is not connected");
    return m_connection->esc(str);
}

bool Database::setCharacterSet(const std::string &characterSet) {
    std::lock_guard<std::mutex> lock(m_queryMutex);
    if (!m_connection || !m_connection->is_open()) throw PGException("Database needs to be connected to change charset.");
    pqxx::work tx(*m_connection);
    tx.exec("SET CLIENT_ENCODING TO " + tx.quote(characterSet));
    tx.commit();
    return true;
}

std::deque<std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>>> Database::abortAllQueries() {
    auto canceled = queryQueue.clear();
    for (auto &pair : canceled) {
        if (!pair.second) continue;
        pair.second->setStatus(QUERY_ABORTED);
        pair.second->setFinished(true);
    }
    return canceled;
}

DatabaseStatus Database::status() const { return m_status; }
unsigned int Database::serverVersion() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get server version when client is not connected to server yet!");
    }
    return m_serverVersion;
}

std::string Database::serverInfo() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get server info when client is not connected to server yet!");
    }
    return m_serverInfo;
}

std::string Database::hostInfo() {
    if (!m_connectionDone || m_status != DATABASE_CONNECTED) {
        throw PGException("Tried to get host info when client is not connected to server yet!");
    }
    return m_hostInfo;
}
size_t Database::queueSize() { return queryQueue.size(); }
bool Database::wasDisconnected() { return disconnected; }

void Database::setShouldAutoReconnect(bool autoReconnect) { shouldAutoReconnect = autoReconnect; }
void Database::setMultiStatements(bool multiStatement) {
    if (multiStatement) {
        throw PGException("pg: PostgreSQL multi-statement result chains are not supported yet");
    }
    useMultiStatements = false;
}
void Database::setCachePreparedStatements(bool) {
    throw PGException("pg: setCachePreparedStatements() is not supported; prepared statements are prepared per execution");
}
void Database::setConnectTimeout(unsigned int timeout) { connectTimeout = timeout; }
void Database::setReadTimeout(unsigned int) {
    throw PGException("pg: setReadTimeout() is not supported by libpq; use PostgreSQL statement_timeout instead");
}

void Database::setWriteTimeout(unsigned int) {
    throw PGException("pg: setWriteTimeout() is not supported by libpq");
}
void Database::setSSLMode(SSLMode mode) { hasSSLMode = true; sslMode = mode; }

void Database::setSSLSettings(const SSLSettings &settings) {
    if (!settings.capath.empty() || !settings.cipher.empty()) {
        throw PGException("pg: capath and cipher SSL settings do not have exact libpq equivalents");
    }
    sslSettings = settings;
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
        queryQueue.close();
        return;
        }
        m_success = true;
        m_connectionError = "";
        m_connectionDone = true;
        m_status = DATABASE_CONNECTED;
        m_connectWakeupVariable.notify_one();
    }
    run();
    {
        std::lock_guard<std::mutex> lock(m_queryMutex);
        m_connection.reset();
    }
    m_canWait = false;
    disconnected = true;
    if (m_status == DATABASE_CONNECTED) m_status = DATABASE_NOT_CONNECTED;
}

bool Database::attemptConnection() {
    try {
        m_connection.reset(new pqxx::connection(buildConnectionString()));
        m_hostInfo = host + ":" + std::to_string(port) + "/" + m_connection->dbname();
        m_serverVersion = m_connection->server_version();
        return m_connection->is_open();
    } catch (const std::exception &error) {
        m_connectionError = error.what();
        m_connection.reset();
        return false;
    }
}

bool Database::attemptReconnect() {
    if (m_shuttingDown) return false;
    m_connection.reset();
    bool success = attemptConnection();
    {
        std::lock_guard<std::mutex> lock(m_reconnectEventMutex);
        m_reconnectEvents.emplace_back(success, success ? "" : m_connectionError);
    }
    return success;
}

std::deque<std::pair<bool, std::string>> Database::takeReconnectEvents() {
    std::lock_guard<std::mutex> lock(m_reconnectEventMutex);
    auto events = m_reconnectEvents;
    m_reconnectEvents.clear();
    return events;
}

void Database::runQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    try {
        if (!m_connection || !m_connection->is_open()) {
            if (!shouldAutoReconnect || !attemptReconnect()) throw PGConnectionException(m_connectionError);
        }
        {
            std::lock_guard<std::mutex> activeLock(m_activeQueryMutex);
            m_activeQueryData = data;
            m_activeConnection = m_connection.get();
        }
        try {
            query->executeStatement(*this, *m_connection, data);
        } catch (...) {
            std::lock_guard<std::mutex> activeLock(m_activeQueryMutex);
            m_activeQueryData.reset();
            m_activeConnection = nullptr;
            throw;
        }
        {
            std::lock_guard<std::mutex> activeLock(m_activeQueryMutex);
            m_activeQueryData.reset();
            m_activeConnection = nullptr;
        }
        if (data->getStatus() == QUERY_ABORTED) {
            data->setResultStatus(QUERY_NONE);
            return;
        }
        data->setResultStatus(QUERY_SUCCESS);
    } catch (const pqxx::broken_connection &error) {
        if (data->getStatus() == QUERY_ABORTED) {
            data->setResultStatus(QUERY_NONE);
            return;
        }
        if (shouldAutoReconnect) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    } catch (const pqxx::sql_error &error) {
        if (data->getStatus() == QUERY_ABORTED) {
            data->setResultStatus(QUERY_NONE);
            return;
        }
        PGConnectionException pgError(error.what(), error.sqlstate());
        if (shouldAutoReconnect && isRetriableError(pgError)) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    } catch (const PGConnectionException &error) {
        if (data->getStatus() == QUERY_ABORTED) {
            data->setResultStatus(QUERY_NONE);
            return;
        }
        if (shouldAutoReconnect && isRetriableError(error)) attemptReconnect();
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
    } catch (const std::exception &error) {
        if (data->getStatus() == QUERY_ABORTED) {
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
        if (!queryQueue.take(pair)) return;
        auto data = pair.second;
        if (data->getStatus() == QUERY_ABORTED) {
            finishedQueries.put(pair);
            data->setFinished(true);
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(m_queryMutex);
            if (data->getStatus() == QUERY_ABORTED) {
                finishedQueries.put(pair);
                data->setFinished(true);
                continue;
            }
            data->setStatus(QUERY_RUNNING);
            runQuery(pair.first, data);
            if (data->getStatus() != QUERY_ABORTED) {
                data->setStatus(QUERY_COMPLETE);
            }
        }
        finishedQueries.put(pair);
        data->setFinished(true);
    }
}

void Database::waitForQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    if (!m_canWait) {
        failWaitingQuery(query, data, "Can not wait on query, database is not connected or connection failed.");
        return;
    }
    if (data->isFinished()) return;
    data->waitUntilFinished();
}

void Database::failWaitingQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data,
                                const std::string &reason) {
    data->setError(reason);
    data->setResultStatus(QUERY_ERROR);
    data->setStatus(QUERY_COMPLETE);
    data->setFinished(true);
    finishedQueries.put(std::make_pair(query, data));
}

bool Database::isRetriableError(const PGConnectionException &error) {
    return (error.sqlstate.size() >= 2 && error.sqlstate.substr(0, 2) == "08" && error.sqlstate != "08007") ||
           error.sqlstate.empty();
}

std::string Database::quoteConninfoValue(const std::string &value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\\' || ch == '\'') out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string Database::buildConnectionString() const {
    if (useRawConnectionString) {
        return rawConnectionString;
    }

    std::ostringstream ss;
    auto add = [&](const std::string &key, const std::string &value) {
        if (!value.empty()) ss << key << "=" << quoteConninfoValue(value) << " ";
    };
    add("host", unixSocket.empty() ? host : unixSocket);
    add("user", username);
    add("password", password);
    add("dbname", database);
    if (port != 0) ss << "port=" << port << " ";
    if (connectTimeout > 0) ss << "connect_timeout=" << connectTimeout << " ";
    if (hasSSLMode) {
        switch (sslMode) {
            case SSL_MODE_DISABLED: ss << "sslmode=disable "; break;
            case SSL_MODE_PREFERRED: ss << "sslmode=prefer "; break;
            case SSL_MODE_REQUIRED: ss << "sslmode=require "; break;
            case SSL_MODE_VERIFY_CA: ss << "sslmode=verify-ca "; break;
            case SSL_MODE_VERIFY_IDENTITY: ss << "sslmode=verify-full "; break;
        }
    }
    add("sslkey", sslSettings.key);
    add("sslcert", sslSettings.cert);
    add("sslrootcert", sslSettings.ca);
    for (const auto &option : optionTable) {
        add(option.first, option.second);
    }
    return ss.str();
}
