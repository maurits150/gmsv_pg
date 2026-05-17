// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_IQUERY_H
#define PG_IQUERY_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <pqxx/pqxx>

#include "PGException.h"

class Database;

enum QueryStatus {
    QUERY_NOT_RUNNING = 0,
    QUERY_RUNNING = 1,
    QUERY_COMPLETE = 3,
    QUERY_ABORTED = 4,
    QUERY_WAITING = 5,
};

enum QueryResultStatus {
    QUERY_NONE = 0,
    QUERY_ERROR,
    QUERY_SUCCESS
};

enum QueryOption {
    OPTION_NUMERIC_FIELDS = 1,
    OPTION_NAMED_FIELDS = 2,
    OPTION_INTERPRET_DATA = 4,
    OPTION_CACHE = 8,
};

class IQueryData;

class IQuery : public std::enable_shared_from_this<IQuery> {
    friend class Database;
    friend class Transaction;

public:
    explicit IQuery(std::shared_ptr<Database> database);
    virtual ~IQuery();

    void start(const std::shared_ptr<IQueryData> &queryData);
    bool isRunning();
    void setOption(int option, bool enabled);
    bool hasOption(int option) const { return (m_options & option) != 0; }
    void addQueryData(const std::shared_ptr<IQueryData> &data);
    void finishQueryData(const std::shared_ptr<IQueryData> &data);
    std::string error() const;
    std::vector<std::shared_ptr<IQueryData>> abort();
    void wait(bool shouldSwap);
    bool hasCallbackData() const { return callbackQueryData != nullptr; }
    QueryResultStatus getResultStatus() const;
    void setCallbackData(std::shared_ptr<IQueryData> data) { callbackQueryData = std::move(data); }

    virtual std::string getSQLString() = 0;

    std::shared_ptr<IQueryData> callbackQueryData;

protected:
    virtual void executeStatement(Database &database, pqxx::connection &connection,
                                  const std::shared_ptr<IQueryData> &data) = 0;
    virtual void executeInTransaction(Database &database, pqxx::connection &connection, pqxx::work &transaction,
                                      const std::shared_ptr<IQueryData> &data) = 0;

    std::shared_ptr<Database> m_database;
    int m_options = OPTION_NAMED_FIELDS | OPTION_INTERPRET_DATA | OPTION_CACHE;
    std::deque<std::shared_ptr<IQueryData>> runningQueryData;
    bool hasBeenStarted = false;
};

class IQueryData : public std::enable_shared_from_this<IQueryData> {
    friend class IQuery;

public:
    virtual ~IQueryData() = default;

    std::string getError() { return m_errorText; }
    void setError(std::string err) { m_errorText = std::move(err); }
    bool isFinished();
    void setFinished(bool isFinished);
    QueryStatus getStatus();
    void setStatus(QueryStatus status);
    QueryResultStatus getResultStatus();
    void setResultStatus(QueryResultStatus status);
    bool isFirstData() const { return m_wasFirstData; }

protected:
    std::string m_errorText;
    std::atomic<bool> finished{false};
    std::atomic<QueryStatus> m_status{QUERY_NOT_RUNNING};
    std::atomic<QueryResultStatus> m_resultStatus{QUERY_NONE};
    bool m_wasFirstData = false;
};

#endif
