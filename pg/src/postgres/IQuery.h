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

#include <libpq-fe.h>

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

class IQuery;
class IQueryData;

struct QueryAbortResult {
    // True when abort either completed queued work immediately or requested cancellation of running work.
    bool requested = false;
    size_t requestedCount = 0;
    // Query executions whose callbacks can be completed synchronously by the caller.
    std::vector<std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>>> completed;
};

class IQuery : public std::enable_shared_from_this<IQuery> {
    friend class Database;
    friend class Transaction;

public:
    explicit IQuery(std::shared_ptr<Database> database);
    virtual ~IQuery();

    void start(const std::shared_ptr<IQueryData> &queryData);
    bool isRunning();
    void setOption(int option, bool enabled);
    bool hasOption(int option) const;
    void snapshotOptions(const std::shared_ptr<IQueryData> &data) const;
    void addQueryData(const std::shared_ptr<IQueryData> &data);
    void finishQueryData(const std::shared_ptr<IQueryData> &data);
    std::string error() const;
    QueryAbortResult abort();
    void wait(bool shouldSwap);
    bool hasCallbackData() const;
    std::shared_ptr<IQueryData> getCallbackData() const;
    std::shared_ptr<Database> database() const { return m_database; }
    QueryResultStatus getResultStatus() const;
    void setCallbackData(std::shared_ptr<IQueryData> data);

    virtual std::string getSQLString() = 0;

    std::shared_ptr<IQueryData> callbackQueryData;

protected:
    virtual void validateStart(const std::shared_ptr<IQueryData> &data);
    virtual void executeStatement(Database &database, PGconn *connection,
                                  const std::shared_ptr<IQueryData> &data) = 0;
    virtual void executeInTransaction(Database &database, PGconn *connection,
                                      const std::shared_ptr<IQueryData> &data) = 0;

    std::shared_ptr<Database> m_database;
    int m_options = OPTION_NAMED_FIELDS | OPTION_INTERPRET_DATA;
    std::deque<std::shared_ptr<IQueryData>> runningQueryData;
    bool hasBeenStarted = false;
    mutable std::mutex m_stateMutex;
};

class IQueryData : public std::enable_shared_from_this<IQueryData> {
    friend class IQuery;

public:
    virtual ~IQueryData() = default;

    std::string getError();
    void setError(std::string err);
    bool isFinished();
    void setFinished(bool isFinished);
    void waitUntilFinished();
    void setCancellationRequested(bool requested);
    bool isCancellationRequested() const;
    QueryStatus getStatus();
    void setStatus(QueryStatus status);
    QueryResultStatus getResultStatus();
    void setResultStatus(QueryResultStatus status);
    bool isFirstData() const { return m_wasFirstData; }
    bool hasOption(int option) const { return (m_optionsSnapshot & option) != 0; }
    void setOptionsSnapshot(int options) { m_optionsSnapshot = options; }

protected:
    std::string m_errorText;
    std::mutex m_errorMutex;
    std::atomic<bool> finished{false};
    std::atomic<bool> cancellationRequested{false};
    std::mutex m_finishMutex;
    std::condition_variable m_finishCondition;
    std::atomic<QueryStatus> m_status{QUERY_NOT_RUNNING};
    std::atomic<QueryResultStatus> m_resultStatus{QUERY_NONE};
    bool m_wasFirstData = false;
    int m_optionsSnapshot = OPTION_NAMED_FIELDS | OPTION_INTERPRET_DATA;
};

#endif
