// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Transaction.h"

#include "Database.h"

std::shared_ptr<TransactionData>
Transaction::buildQueryData(const std::deque<std::pair<std::shared_ptr<Query>, std::shared_ptr<IQueryData>>> &queries) {
    return std::shared_ptr<TransactionData>(new TransactionData(queries));
}

std::shared_ptr<Transaction> Transaction::create(const std::shared_ptr<Database> &database) {
    return std::shared_ptr<Transaction>(new Transaction(database));
}

void Transaction::executeStatement(Database &database, pqxx::connection &connection,
                                   const std::shared_ptr<IQueryData> &ptr) {
    auto data = std::dynamic_pointer_cast<TransactionData>(ptr);
    try {
        pqxx::work tx(connection);
        executeInTransaction(database, connection, tx, ptr);
        tx.commit();

        for (auto &pair : data->m_queries) {
            pair.second->setResultStatus(QUERY_SUCCESS);
            pair.second->setStatus(QUERY_COMPLETE);
        }
        data->setResultStatus(QUERY_SUCCESS);
    } catch (const std::exception &error) {
        for (auto &pair : data->m_queries) {
            pair.second->setResultStatus(QUERY_ERROR);
            pair.second->setStatus(QUERY_COMPLETE);
            pair.second->setError(std::string("Transaction rolled back: ") + error.what());
        }
        data->setResultStatus(QUERY_ERROR);
        data->setError(error.what());
        throw;
    }
}

void Transaction::executeInTransaction(Database &database, pqxx::connection &connection, pqxx::work &tx,
                                       const std::shared_ptr<IQueryData> &ptr) {
    auto data = std::dynamic_pointer_cast<TransactionData>(ptr);
    for (auto &pair : data->m_queries) {
        pair.second->setStatus(QUERY_RUNNING);
        pair.first->executeInTransaction(database, connection, tx, pair.second);
    }
}
