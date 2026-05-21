// Owns direct libpq protocol execution and PGresult conversion for gmsv_pg.
#include "LibpqExecutor.h"

#include <cstdlib>
#include <utility>

#include "Query.h"

static bool containsNul(const std::string &value) {
    return value.find('\0') != std::string::npos;
}

void LibpqExecutor::executeSimple(PGconn *connection, const std::string &sql, QueryData &data) {
    if (containsNul(sql)) throw PGException("pg: SQL strings cannot contain embedded NUL bytes");
    if (PQsendQuery(connection, sql.c_str()) != 1) {
        throw PGConnectionException(PQerrorMessage(connection));
    }
    drainResults(connection, &data);
}

void LibpqExecutor::executeSingleStatement(PGconn *connection, const std::string &sql, QueryData &data) {
    static const std::vector<LibpqParameter> noParameters;
    executeParams(connection, sql, noParameters, data);
}

void LibpqExecutor::executeParams(PGconn *connection, const std::string &sql,
                                  const std::vector<LibpqParameter> &parameters, QueryData &data) {
    if (containsNul(sql)) throw PGException("pg: SQL strings cannot contain embedded NUL bytes");
    std::vector<const char *> values;
    std::vector<int> lengths;
    std::vector<int> formats;
    values.reserve(parameters.size());
    lengths.reserve(parameters.size());
    formats.reserve(parameters.size());

    for (const auto &parameter : parameters) {
        if (parameter.type == LIBPQ_PARAMETER_NULL) {
            values.push_back(nullptr);
            lengths.push_back(0);
        } else {
            values.push_back(parameter.value.c_str());
            lengths.push_back(static_cast<int>(parameter.value.size()));
        }
        formats.push_back(0); // Text parameters preserve current Lua string/number semantics.
    }

    if (PQsendQueryParams(connection, sql.c_str(), static_cast<int>(parameters.size()), nullptr,
                          values.empty() ? nullptr : values.data(),
                          lengths.empty() ? nullptr : lengths.data(),
                          formats.empty() ? nullptr : formats.data(), 0) != 1) {
        throw PGConnectionException(PQerrorMessage(connection));
    }
    drainResults(connection, &data);
}

void LibpqExecutor::executeCommand(PGconn *connection, const std::string &sql) {
    if (containsNul(sql)) throw PGException("pg: SQL strings cannot contain embedded NUL bytes");
    if (PQsendQuery(connection, sql.c_str()) != 1) {
        throw PGConnectionException(PQerrorMessage(connection));
    }
    drainResults(connection, nullptr);
}

void LibpqExecutor::drainResults(PGconn *connection, QueryData *data) {
    PGresult *result = nullptr;
    while ((result = PQgetResult(connection)) != nullptr) {
        ExecStatusType status = PQresultStatus(result);
        if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT || status == PGRES_COPY_BOTH) {
            PQclear(result);
            drainUnsupportedCopy(connection, status);
            throw PGConnectionException("pg: COPY queries are not supported through db:query(); connection was reset");
        }
        if (!isSuccessStatus(status)) {
            try {
                throwResultError(connection, result);
            } catch (...) {
                PQclear(result);
                while ((result = PQgetResult(connection)) != nullptr) PQclear(result);
                throw;
            }
        }
        if (data) appendResult(result, *data);
        PQclear(result);
    }

    if (PQstatus(connection) == CONNECTION_BAD) {
        throw PGConnectionException(PQerrorMessage(connection));
    }
}

void LibpqExecutor::drainUnsupportedCopy(PGconn *connection, ExecStatusType status) {
    (void) status;
    PQreset(connection);
}

void LibpqExecutor::appendResult(PGresult *result, QueryData &data) {
    StatementResult statement;
    statement.rows = ResultData(result);
    statement.affectedRows = parseUnsigned(PQcmdTuples(result));
    statement.oid = static_cast<unsigned long long>(PQoidValue(result));
    const char *status = PQcmdStatus(result);
    statement.commandStatus = status ? status : "";
    data.addStatementResult(std::move(statement));
}

void LibpqExecutor::throwResultError(PGconn *connection, PGresult *result) {
    const char *message = PQresultErrorMessage(result);
    std::string error = (message && *message) ? message : PQerrorMessage(connection);
    const char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
    std::string state = sqlstate ? sqlstate : "";
    if (PQstatus(connection) == CONNECTION_BAD || (state.size() >= 2 && state.substr(0, 2) == "08")) {
        throw PGConnectionException(error, state);
    }
    throw PGException(error, state);
}

bool LibpqExecutor::isSuccessStatus(ExecStatusType status) {
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK || status == PGRES_EMPTY_QUERY;
}

unsigned long long LibpqExecutor::parseUnsigned(const char *value) {
    if (!value || !*value) return 0;
    return std::strtoull(value, nullptr, 10);
}
