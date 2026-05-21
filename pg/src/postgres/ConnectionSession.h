// Owns a live PostgreSQL connection session for gmsv_pg.
#ifndef PG_CONNECTIONSESSION_H
#define PG_CONNECTIONSESSION_H

#include <string>

#include <libpq-fe.h>

#include "ConnectionConfig.h"

// Runtime representation of a connected PostgreSQL session and its metadata.
class ConnectionSession {
public:
    ~ConnectionSession();

    bool connect(const ConnectionConfig &config);
    void reset();

    bool isOpen() const;
    PGconn *connection();

    bool ping();
    std::string escape(const std::string &str);
    bool setCharacterSet(const std::string &characterSet);

    std::string error() const { return connectionError; }
    unsigned int serverVersion() const { return serverVersionValue; }
    std::string serverInfo() const { return serverInfoValue; }
    std::string hostInfo() const { return hostInfoValue; }

private:
    void updateMetadata();

    PGconn *connectionHandle = nullptr;
    std::string connectionError;
    std::string serverInfoValue = "PostgreSQL";
    std::string hostInfoValue;
    unsigned int serverVersionValue = 0;
};

#endif
