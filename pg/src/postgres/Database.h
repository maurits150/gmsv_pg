// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_DATABASE_H
#define PG_DATABASE_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ActiveQueryState.h"
#include "ConnectionConfig.h"
#include "ConnectionSession.h"
#include "IQuery.h"
#include "QueryWorker.h"
#include "ReconnectLog.h"

class PreparedQuery;
class Query;
class Transaction;

enum DatabaseStatus {
    DATABASE_CONNECTED = 0,
    DATABASE_CONNECTING = 1,
    DATABASE_NOT_CONNECTED = 2,
    DATABASE_CONNECTION_FAILED = 3
};

class Database : public std::enable_shared_from_this<Database> {
    friend class IQuery;

public:
    static std::shared_ptr<Database> createDatabase(const std::string &host, const std::string &username,
                                                    const std::string &password, const std::string &database,
                                                    unsigned int port, const std::string &unixSocket);
    static std::shared_ptr<Database> createDatabaseFromConnectionString(const std::string &connectionString);
    static std::shared_ptr<Database> createDatabaseFromOptions(const std::vector<std::pair<std::string, std::string>> &options);
    ~Database();

    void enqueueQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    bool swapQueryToFront(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    QueryAbortResult abortQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    // Requests PostgreSQL cancellation for the query data currently executing on the worker connection.
    bool cancelRunningQuery(const std::shared_ptr<IQueryData> &data);
    void addActiveQueryAlias(const std::shared_ptr<IQueryData> &data);
    void removeActiveQueryAlias(const std::shared_ptr<IQueryData> &data);

    std::shared_ptr<Query> query(const std::string &query);
    std::shared_ptr<PreparedQuery> prepare(const std::string &query);
    std::shared_ptr<Transaction> transaction();

    void connect();
    void wait();
    void disconnect(bool wait);
    bool ping();
    std::string escape(const std::string &str);
    bool setCharacterSet(const std::string &characterSet);

    QueryAbortResult abortAllQueries();
    std::deque<std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>>> takeFinishedQueries() {
        return m_worker.takeFinished();
    }

    DatabaseStatus status() const;
    unsigned int serverVersion();
    std::string serverInfo();
    std::string hostInfo();
    size_t queueSize();
    bool wasDisconnected();
    bool isConnectionDone() { return m_connectionDone; }
    bool connectionSuccessful() { return m_success; }
    std::string connectionError();
    bool attemptReconnect();
    std::deque<std::pair<bool, std::string>> takeReconnectEvents();

    void setAutoReconnect(bool autoReconnect);
    void setMultiStatements(bool enabled);
    bool multiStatementsEnabled() const;
    void setConnectTimeout(unsigned int timeout);
    void setSSLMode(SSLMode mode);
    void setSSLSettings(const SSLSettings &settings);

    static bool isRetriableError(const PGConnectionException &error);
    void waitForQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);

private:
    Database(std::string host, std::string username, std::string password, std::string database,
             unsigned int port, std::string unixSocket);
    explicit Database(std::string connectionString);
    explicit Database(std::vector<std::pair<std::string, std::string>> options);

    void shutdown();
    void connectRun();
    void run();
    void runQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    void completeQueryWithError(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data,
                           const std::string &reason);
    bool attemptConnection();
    bool attemptConnectionUnlocked();
    void setInFlight(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    void clearInFlight(const std::shared_ptr<IQueryData> &data);
    std::shared_ptr<IQueryData> currentInFlightData();

    QueryWorker m_worker;
    ConnectionConfig m_config;
    ConnectionSession m_session;
    ReconnectLog m_reconnectLog;
    ActiveQueryState m_activeQuery;
    std::mutex m_inFlightMutex;
    std::shared_ptr<IQuery> m_inFlightQuery;
    std::shared_ptr<IQueryData> m_inFlightData;
    std::thread m_thread;
    std::mutex m_connectMutex;
    std::recursive_mutex m_queryMutex;
    std::condition_variable m_connectWakeupVariable;

    std::atomic<bool> shouldAutoReconnect{true};
    // Raw db:query execution defaults to PostgreSQL simple-query mode for result chains.
    std::atomic<bool> m_multiStatements{true};
    std::atomic<bool> startedConnecting{false};
    std::atomic<bool> m_canWait{false};
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_success{true};
    std::atomic<bool> disconnected{false};
    std::atomic<bool> m_connectionDone{false};
    std::atomic<DatabaseStatus> m_status{DATABASE_NOT_CONNECTED};

};

#endif
