// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Query.h"

#include <cctype>
#include <utility>

#include "Database.h"
#include "LibpqExecutor.h"

static std::string leadingSqlKeyword(const std::string &sql) {
    size_t index = 0;
    while (index < sql.size()) {
        while (index < sql.size() && std::isspace(static_cast<unsigned char>(sql[index]))) ++index;
        if (index + 1 < sql.size() && sql[index] == '-' && sql[index + 1] == '-') {
            index += 2;
            while (index < sql.size() && sql[index] != '\n' && sql[index] != '\r') ++index;
            continue;
        }
        if (index + 1 < sql.size() && sql[index] == '/' && sql[index + 1] == '*') {
            index += 2;
            while (index + 1 < sql.size() && !(sql[index] == '*' && sql[index + 1] == '/')) ++index;
            if (index + 1 < sql.size()) index += 2;
            continue;
        }
        break;
    }
    size_t start = index;
    while (index < sql.size() && std::isalpha(static_cast<unsigned char>(sql[index]))) ++index;
    std::string keyword = sql.substr(start, index - start);
    for (auto &ch : keyword) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return keyword;
}

static bool isTransactionControlSql(const std::string &sql) {
    std::string keyword = leadingSqlKeyword(sql);
    return keyword == "begin" || keyword == "start" || keyword == "commit" ||
           keyword == "end" || keyword == "rollback" || keyword == "abort";
}

Query::Query(const std::shared_ptr<Database> &database, std::string query)
    : IQuery(database), m_query(std::move(query)) {}

Query::~Query() = default;

void Query::executeStatement(Database &database, PGconn *connection,
                             const std::shared_ptr<IQueryData> &data) {
    auto queryData = std::dynamic_pointer_cast<QueryData>(data);
    if (queryData->multiStatementsEnabled()) {
        LibpqExecutor::executeSimple(connection, m_query, *queryData);
    } else {
        LibpqExecutor::executeSingleStatement(connection, m_query, *queryData);
    }
}

void Query::executeInTransaction(Database &database, PGconn *connection,
                                  const std::shared_ptr<IQueryData> &data) {
    auto queryData = std::dynamic_pointer_cast<QueryData>(data);
    (void) database;
    if (isTransactionControlSql(m_query)) throw PGException("pg: transaction control SQL is not allowed inside transaction child queries");
    LibpqExecutor::executeSingleStatement(connection, m_query, *queryData);
}

unsigned long long Query::affectedRows() {
    auto data = std::dynamic_pointer_cast<QueryData>(getCallbackData());
    if (!data) return 0;
    return data->getAffectedRows();
}

unsigned long long Query::oid() {
    auto data = std::dynamic_pointer_cast<QueryData>(getCallbackData());
    if (!data) return 0;
    return data->getOid();
}

std::string Query::commandStatus() {
    auto data = std::dynamic_pointer_cast<QueryData>(getCallbackData());
    if (!data) return "";
    return data->getCommandStatus();
}

std::shared_ptr<Query> Query::create(const std::shared_ptr<Database> &database, const std::string &query) {
    return std::shared_ptr<Query>(new Query(database, query));
}

bool Query::multiStatementsForNewExecution() const {
    return m_database && m_database->multiStatementsEnabled();
}

unsigned long long QueryData::getAffectedRows() const {
    return m_results.empty() ? 0 : m_results[m_currentResult].affectedRows;
}

unsigned long long QueryData::getOid() const {
    return m_results.empty() ? 0 : m_results[m_currentResult].oid;
}

std::string QueryData::getCommandStatus() const {
    return m_results.empty() ? "" : m_results[m_currentResult].commandStatus;
}

bool QueryData::hasMoreResults() const {
    return !m_results.empty() && m_currentResult + 1 < m_results.size();
}

bool QueryData::advanceResult() {
    if (!hasMoreResults()) return false;
    ++m_currentResult;
    return true;
}

ResultData &QueryData::getResult() {
    if (m_results.empty()) throw PGException("pg: query has no result data");
    return m_results[m_currentResult].rows;
}

void QueryData::addStatementResult(StatementResult result) {
    m_results.push_back(std::move(result));
}
