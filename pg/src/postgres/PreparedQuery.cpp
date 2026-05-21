// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "PreparedQuery.h"

#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

PreparedQuery::PreparedQuery(const std::shared_ptr<Database> &database, std::string query)
    : Query(database, std::move(query)) {}

PreparedQuery::~PreparedQuery() = default;

void PreparedQuery::clearParameters() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_parameters.clear();
}

void PreparedQuery::setNumber(unsigned int index, double value) {
    if (index < 1) throw PGException("Index must be greater than 0");
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_NUMBER, numberToString(value));
}

void PreparedQuery::setString(unsigned int index, const std::string &value) {
    if (index < 1) throw PGException("Index must be greater than 0");
    if (value.find('\0') != std::string::npos) {
        throw PGException("pg: prepared text parameters cannot contain embedded NUL bytes");
    }
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_STRING, value);
}

void PreparedQuery::setBoolean(unsigned int index, bool value) {
    if (index < 1) throw PGException("Index must be greater than 0");
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_BOOLEAN, value ? "true" : "false");
}

void PreparedQuery::setNull(unsigned int index) {
    if (index < 1) throw PGException("Index must be greater than 0");
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_parameters[index] = std::make_shared<PreparedQueryField>(index, PREPARED_FIELD_NULL);
}

PreparedParameterMap PreparedQuery::snapshotParameters() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_parameters;
}

std::shared_ptr<PreparedQuery> PreparedQuery::create(const std::shared_ptr<Database> &database, std::string query) {
    return std::shared_ptr<PreparedQuery>(new PreparedQuery(database, std::move(query)));
}

void PreparedQuery::executeStatement(Database &database, PGconn *connection,
                                     const std::shared_ptr<IQueryData> &ptr) {
    (void) database;
    auto data = std::dynamic_pointer_cast<PreparedQueryData>(ptr);
    unsigned int count = maxParameterIndex(data->m_parameters);
    std::vector<LibpqParameter> parameters;
    parameters.reserve(count);
    for (unsigned int i = 1; i <= count; ++i) {
        auto it = data->m_parameters.find(i);
        LibpqParameter parameter;
        if (it != data->m_parameters.end() && it->second->m_type != PREPARED_FIELD_NULL) {
            parameter.type = LIBPQ_PARAMETER_TEXT;
            parameter.value = it->second->m_value;
        }
        parameters.push_back(std::move(parameter));
    }
    LibpqExecutor::executeParams(connection, m_query, parameters, *data);
}

void PreparedQuery::executeInTransaction(Database &, PGconn *connection,
                                           const std::shared_ptr<IQueryData> &ptr) {
    if (isTransactionControlSql(m_query)) throw PGException("pg: transaction control SQL is not allowed inside transaction child queries");
    auto data = std::dynamic_pointer_cast<PreparedQueryData>(ptr);
    unsigned int count = maxParameterIndex(data->m_parameters);
    std::vector<LibpqParameter> parameters;
    parameters.reserve(count);
    for (unsigned int i = 1; i <= count; ++i) {
        auto it = data->m_parameters.find(i);
        LibpqParameter parameter;
        if (it != data->m_parameters.end() && it->second->m_type != PREPARED_FIELD_NULL) {
            parameter.type = LIBPQ_PARAMETER_TEXT;
            parameter.value = it->second->m_value;
        }
        parameters.push_back(std::move(parameter));
    }
    LibpqExecutor::executeParams(connection, m_query, parameters, *data);
}

unsigned int PreparedQuery::maxParameterIndex(const PreparedParameterMap &params) {
    unsigned int max = 0;
    for (auto &pair : params) if (pair.first > max) max = pair.first;
    return max;
}

std::string PreparedQuery::numberToString(double value) {
    if (!std::isfinite(value)) throw PGException("pg: prepared numeric parameters must be finite");
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss.precision(17);
    ss << value;
    return ss.str();
}
