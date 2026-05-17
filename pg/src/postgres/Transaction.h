// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_TRANSACTION_H
#define PG_TRANSACTION_H

#include <deque>
#include <memory>
#include <utility>

#include "Query.h"

class TransactionData : public IQueryData {
    friend class Transaction;

public:
    std::deque<std::pair<std::shared_ptr<Query>, std::shared_ptr<IQueryData>>> m_queries;

protected:
    explicit TransactionData(std::deque<std::pair<std::shared_ptr<Query>, std::shared_ptr<IQueryData>>> queries)
        : m_queries(std::move(queries)) {}
};

class Transaction : public IQuery {
    friend class Database;

public:
    static std::shared_ptr<Transaction> create(const std::shared_ptr<Database> &database);
    std::string getSQLString() override { return ""; }

protected:
    void validateStart(const std::shared_ptr<IQueryData> &data) override;
    void executeStatement(Database &database, pqxx::connection &connection,
                          const std::shared_ptr<IQueryData> &data) override;
    void executeInTransaction(Database &database, pqxx::connection &connection, pqxx::work &transaction,
                              const std::shared_ptr<IQueryData> &data) override;
    explicit Transaction(const std::shared_ptr<Database> &database) : IQuery(database) {}
};

#endif
