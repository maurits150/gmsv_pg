// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "PreparedQuery.h"

#include <atomic>
#include <sstream>
#include <utility>

#include "Database.h"

PreparedQuery::PreparedQuery(const std::shared_ptr<Database> &database, std::string query)
    : Query(database, std::move(query)) {}

PreparedQuery::~PreparedQuery() = default;

void PreparedQuery::clearParameters() {
    m_parameters.clear();
}

void PreparedQuery::setNumber(unsigned int index, double value) {
    if (index < 1) throw PGException("Index must be greater than 0");
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_NUMBER, numberToString(value));
}

void PreparedQuery::setString(unsigned int index, const std::string &value) {
    if (index < 1) throw PGException("Index must be greater than 0");
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_STRING, value);
}

void PreparedQuery::setBoolean(unsigned int index, bool value) {
    if (index < 1) throw PGException("Index must be greater than 0");
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_BOOLEAN, value ? "true" : "false");
}

void PreparedQuery::setNull(unsigned int index) {
    if (index < 1) throw PGException("Index must be greater than 0");
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_NULL);
}

void PreparedQuery::putNewParameters() {
    throw PGException("pg: putNewParameters() is not supported until PostgreSQL multi-result access is implemented");
}

PreparedParameterMap PreparedQuery::snapshotParameters() const {
    return m_parameters;
}

std::shared_ptr<PreparedQuery> PreparedQuery::create(const std::shared_ptr<Database> &database, std::string query) {
    return std::shared_ptr<PreparedQuery>(new PreparedQuery(database, std::move(query)));
}

void PreparedQuery::executeStatement(Database &database, pqxx::connection &connection,
                                     const std::shared_ptr<IQueryData> &ptr) {
    pqxx::work tx(connection);
    executeInTransaction(database, connection, tx, ptr);
    tx.commit();
}

void PreparedQuery::executeInTransaction(Database &, pqxx::connection &connection, pqxx::work &tx,
                                         const std::shared_ptr<IQueryData> &ptr) {
    auto data = std::dynamic_pointer_cast<PreparedQueryData>(ptr);
    static std::atomic<unsigned long> statementCounter{0};
    std::string statementName = "gmsv_pg_stmt_" + std::to_string(++statementCounter);

    connection.prepare(statementName, m_query);
    try {
        pqxx::prepare::invocation invocation = tx.prepared(statementName);
        unsigned int count = maxParameterIndex(data->m_parameters);
        for (unsigned int i = 1; i <= count; ++i) {
            auto it = data->m_parameters.find(i);
            if (it == data->m_parameters.end() || it->second->m_type == PREPARED_FIELD_NULL) invocation();
            else invocation(it->second->m_value);
        }
        pqxx::result result = invocation.exec();
        data->m_results.emplace_back(result);
        data->m_affectedRows.push_back(result.affected_rows());
        connection.unprepare(statementName);
    } catch (...) {
        try { connection.unprepare(statementName); } catch (...) {}
        throw;
    }
}

unsigned int PreparedQuery::maxParameterIndex(const PreparedParameterMap &params) {
    unsigned int max = 0;
    for (auto &pair : params) if (pair.first > max) max = pair.first;
    return max;
}

std::string PreparedQuery::numberToString(double value) {
    std::ostringstream ss;
    ss.precision(17);
    ss << value;
    return ss.str();
}
