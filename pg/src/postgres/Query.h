// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_QUERY_H
#define PG_QUERY_H

#include <deque>

#include "IQuery.h"
#include "ResultData.h"

class QueryData;
class LibpqExecutor;

class Query : public IQuery {
    friend class Database;
    friend class Transaction;

public:
    ~Query() override;

    void executeStatement(Database &database, PGconn *connection,
                          const std::shared_ptr<IQueryData> &data) override;
    void executeInTransaction(Database &database, PGconn *connection,
                              const std::shared_ptr<IQueryData> &data) override;

    unsigned long long affectedRows();
    unsigned long long oid();
    std::string commandStatus();
    bool multiStatementsForNewExecution() const;
    std::string getSQLString() override { return m_query; }

    static std::shared_ptr<Query> create(const std::shared_ptr<Database> &database, const std::string &query);

protected:
    Query(const std::shared_ptr<Database> &database, std::string query);

    std::string m_query;
};

class QueryData : public IQueryData {
    friend class Query;
    friend class PreparedQuery;
    friend class Transaction;
    friend class LibpqExecutor;

public:
    unsigned long long getLastInsertID() const { return 0; }
    unsigned long long getAffectedRows() const;
    unsigned long long getOid() const;
    std::string getCommandStatus() const;
    bool hasAnyResults() const { return !m_results.empty(); }
    bool hasMoreResults() const;
    bool advanceResult();
    void setMultiStatementsEnabled(bool enabled) { m_multiStatementsEnabled = enabled; }
    bool multiStatementsEnabled() const { return m_multiStatementsEnabled; }
    ResultData &getResult();
    std::deque<StatementResult> getResults() { return m_results; }
    void addStatementResult(StatementResult result);

protected:
    QueryData() = default;

    std::deque<StatementResult> m_results;
    size_t m_currentResult = 0;
    bool m_multiStatementsEnabled = true;
};

#endif
