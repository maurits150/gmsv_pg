// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Query.h"

#include <utility>

#include "Database.h"

Query::Query(const std::shared_ptr<Database> &database, std::string query)
    : IQuery(database), m_query(std::move(query)) {}

Query::~Query() = default;

void Query::executeStatement(Database &database, pqxx::connection &connection,
                             const std::shared_ptr<IQueryData> &data) {
    pqxx::work tx(connection);
    executeInTransaction(database, connection, tx, data);
    tx.commit();
}

void Query::executeInTransaction(Database &, pqxx::connection &, pqxx::work &transaction,
                                 const std::shared_ptr<IQueryData> &data) {
    auto queryData = std::dynamic_pointer_cast<QueryData>(data);
    pqxx::result result = transaction.exec_params(m_query);
    queryData->m_oids.push_back(result.inserted_oid());
    queryData->m_results.emplace_back(result);
    queryData->m_affectedRows.push_back(result.affected_rows());
}

unsigned long long Query::affectedRows() {
    if (!hasCallbackData()) return 0;
    auto data = std::dynamic_pointer_cast<QueryData>(callbackQueryData);
    return data->getAffectedRows();
}

unsigned long long Query::oid() {
    if (!hasCallbackData()) return 0;
    auto data = std::dynamic_pointer_cast<QueryData>(callbackQueryData);
    return data->m_oids.empty() ? 0 : data->m_oids.front();
}

std::shared_ptr<Query> Query::create(const std::shared_ptr<Database> &database, const std::string &query) {
    return std::shared_ptr<Query>(new Query(database, query));
}
