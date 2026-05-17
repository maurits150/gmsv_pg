// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_QUERY_H
#define PG_QUERY_H

#include <deque>

#include "IQuery.h"
#include "ResultData.h"

class QueryData;

class Query : public IQuery {
    friend class Database;
    friend class Transaction;

public:
    ~Query() override;

    void executeStatement(Database &database, pqxx::connection &connection,
                          const std::shared_ptr<IQueryData> &data) override;
    void executeInTransaction(Database &database, pqxx::connection &connection, pqxx::work &transaction,
                              const std::shared_ptr<IQueryData> &data) override;

    unsigned long long lastInsert();
    unsigned long long affectedRows();
    std::string commandStatus();
    unsigned long long oid();
    bool hasMoreResults();
    void getNextResults();
    virtual std::shared_ptr<QueryData> buildQueryData();
    int m_dataReference = 0;
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

public:
    unsigned long long getLastInsertID() const { return 0; }
    unsigned long long getAffectedRows() const { return m_affectedRows.empty() ? 0 : m_affectedRows.front(); }
    bool hasMoreResults() const { return !m_results.empty(); }
    bool getNextResults();
    ResultData &getResult() { return m_results.front(); }
    std::deque<ResultData> getResults() { return m_results; }

protected:
    QueryData() = default;

    std::deque<unsigned long long> m_affectedRows;
    std::deque<unsigned long long> m_oids;
    std::deque<ResultData> m_results;
};

#endif
