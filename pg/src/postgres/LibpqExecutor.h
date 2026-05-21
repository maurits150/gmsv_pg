// Owns direct libpq protocol execution and PGresult conversion for gmsv_pg.
#ifndef PG_LIBPQEXECUTOR_H
#define PG_LIBPQEXECUTOR_H

#include <string>
#include <vector>

#include <libpq-fe.h>

#include "PGException.h"
#include "ResultData.h"

class QueryData;

enum LibpqParameterType {
    LIBPQ_PARAMETER_TEXT,
    LIBPQ_PARAMETER_NULL,
};

struct LibpqParameter {
    LibpqParameterType type = LIBPQ_PARAMETER_NULL;
    std::string value;
};

// Infrastructure owner for libpq send/drain semantics.
class LibpqExecutor {
public:
    static void executeSimple(PGconn *connection, const std::string &sql, QueryData &data);
    static void executeSingleStatement(PGconn *connection, const std::string &sql, QueryData &data);
    static void executeParams(PGconn *connection, const std::string &sql,
                              const std::vector<LibpqParameter> &parameters, QueryData &data);
    static void executeCommand(PGconn *connection, const std::string &sql);

private:
    static void drainResults(PGconn *connection, QueryData *data);
    static void appendResult(PGresult *result, QueryData &data);
    static void drainUnsupportedCopy(PGconn *connection, ExecStatusType status);
    static void throwResultError(PGconn *connection, PGresult *result);
    static bool isSuccessStatus(ExecStatusType status);
    static unsigned long long parseUnsigned(const char *value);
};

#endif
