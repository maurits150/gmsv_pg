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

#include <pqxx/pqxx>

#include "../BlockingQueue.h"
#include "IQuery.h"

class PreparedQuery;
class Query;
class Transaction;

enum DatabaseStatus {
    DATABASE_CONNECTED = 0,
    DATABASE_CONNECTING = 1,
    DATABASE_NOT_CONNECTED = 2,
    DATABASE_CONNECTION_FAILED = 3
};

enum SSLMode {
    SSL_MODE_DISABLED = 0,
    SSL_MODE_PREFERRED = 1,
    SSL_MODE_REQUIRED = 2,
    SSL_MODE_VERIFY_CA = 3,
    SSL_MODE_VERIFY_IDENTITY = 4,
};

struct SSLSettings {
    std::string key;
    std::string cert;
    std::string ca;
    std::string capath;
    std::string cipher;
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
        return finishedQueries.clear();
    }

    DatabaseStatus status() const;
    unsigned int serverVersion();
    std::string serverInfo();
    std::string hostInfo();
    size_t queueSize();
    bool wasDisconnected();
    bool isConnectionDone() { return m_connectionDone; }
    bool connectionSuccessful() { return m_success; }
    std::string connectionError() { return m_connectionError; }
    bool attemptReconnect();
    std::deque<std::pair<bool, std::string>> takeReconnectEvents();

    void setShouldAutoReconnect(bool autoReconnect);
    void setMultiStatements(bool multiStatement);
    bool allowsMultiStatements() const { return useMultiStatements; }
    void setCachePreparedStatements(bool cachePreparedStatements);
    bool shouldCachePreparedStatements() const { return cachePreparedStatements; }
    void setConnectTimeout(unsigned int timeout);
    void setReadTimeout(unsigned int timeout);
    void setWriteTimeout(unsigned int timeout);
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
    void completeQueuedQueriesWithError(const std::string &reason);
    void failWaitingQuery(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data,
                          const std::string &reason);
    bool attemptConnection();
    std::string buildConnectionString() const;
    static std::string quoteConninfoValue(const std::string &value);

    BlockingQueue<std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>>> finishedQueries;
    BlockingQueue<std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>>> queryQueue;
    std::unique_ptr<pqxx::connection> m_connection;
    std::thread m_thread;
    std::mutex m_connectMutex;
    std::mutex m_queryMutex;
    std::mutex m_activeQueryMutex;
    std::mutex m_reconnectEventMutex;
    std::condition_variable m_connectWakeupVariable;

    std::string m_connectionError;
    std::deque<std::pair<bool, std::string>> m_reconnectEvents;
    // Published only while the worker thread is inside PostgreSQL execution.
    std::shared_ptr<IQueryData> m_activeQueryData;
    pqxx::connection *m_activeConnection = nullptr;
    std::string m_serverInfo = "PostgreSQL";
    std::string m_hostInfo;
    unsigned int m_serverVersion = 0;

    std::atomic<bool> shouldAutoReconnect{true};
    bool useMultiStatements = false;
    bool startedConnecting = false;
    std::atomic<bool> m_canWait{false};
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_success{true};
    std::atomic<bool> disconnected{false};
    std::atomic<bool> m_connectionDone{false};
    std::atomic<bool> cachePreparedStatements{true};
    std::atomic<DatabaseStatus> m_status{DATABASE_NOT_CONNECTED};

    std::string database;
    std::string host;
    std::string username;
    std::string password;
    std::string unixSocket;
    unsigned int port;
    bool useRawConnectionString = false;
    std::string rawConnectionString;
    std::vector<std::pair<std::string, std::string>> optionTable;
    bool hasSSLMode = false;
    SSLMode sslMode = SSL_MODE_PREFERRED;
    SSLSettings sslSettings;
    unsigned int connectTimeout = 0;
};

#endif
