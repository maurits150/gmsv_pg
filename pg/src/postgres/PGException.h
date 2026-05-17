// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_EXCEPTION_H
#define PG_EXCEPTION_H

#include <stdexcept>
#include <string>

class PGException : public std::runtime_error {
public:
    explicit PGException(const std::string &message)
        : std::runtime_error(message), message(message) {}

    std::string message;
};

class PGConnectionException : public PGException {
public:
    PGConnectionException(const std::string &message, const std::string &sqlstate = "")
        : PGException(message), sqlstate(sqlstate) {}

    std::string sqlstate;
};

#endif
