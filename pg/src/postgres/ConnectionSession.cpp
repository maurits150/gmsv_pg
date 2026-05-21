// Owns a live PostgreSQL connection session for gmsv_pg.
#include "ConnectionSession.h"

#include "LibpqExecutor.h"
#include "PGException.h"

ConnectionSession::~ConnectionSession() { reset(); }

bool ConnectionSession::connect(const ConnectionConfig &config) {
    reset();
    connectionHandle = PQconnectdb(config.buildConnectionString().c_str());
    if (!connectionHandle) {
        connectionError = "Failed to allocate PostgreSQL connection";
        return false;
    }

    if (PQstatus(connectionHandle) != CONNECTION_OK) {
        connectionError = PQerrorMessage(connectionHandle);
        reset();
        return false;
    }

    try {
        updateMetadata();
        connectionError.clear();
        return true;
    } catch (const std::exception &error) {
        connectionError = error.what();
        reset();
        return false;
    }
}

void ConnectionSession::reset() {
    if (connectionHandle) {
        PQfinish(connectionHandle);
        connectionHandle = nullptr;
    }
}

bool ConnectionSession::isOpen() const { return connectionHandle && PQstatus(connectionHandle) == CONNECTION_OK; }

PGconn *ConnectionSession::connection() { return connectionHandle; }

bool ConnectionSession::ping() {
    if (!isOpen()) return false;
    try {
        LibpqExecutor::executeCommand(connectionHandle, "SELECT 1");
        return true;
    } catch (...) {
        return false;
    }
}

std::string ConnectionSession::escape(const std::string &str) {
    if (!isOpen()) throw PGException("Cannot escape using database that is not connected");
    std::string escaped;
    escaped.resize(str.size() * 2 + 1);
    int error = 0;
    size_t length = PQescapeStringConn(connectionHandle, &escaped[0], str.c_str(), str.size(), &error);
    if (error != 0) throw PGException(PQerrorMessage(connectionHandle));
    escaped.resize(length);
    return escaped;
}

bool ConnectionSession::setCharacterSet(const std::string &characterSet) {
    if (!isOpen()) throw PGException("Database needs to be connected to change charset.");
    if (PQsetClientEncoding(connectionHandle, characterSet.c_str()) != 0) {
        throw PGException(PQerrorMessage(connectionHandle));
    }
    return true;
}

void ConnectionSession::updateMetadata() {
    const char *connectedHost = PQhost(connectionHandle);
    const char *connectedPort = PQport(connectionHandle);
    const char *connectedDatabase = PQdb(connectionHandle);
    hostInfoValue = connectedHost && *connectedHost ? connectedHost : "local";
    if (connectedPort && *connectedPort) hostInfoValue += std::string(":") + connectedPort;
    if (connectedDatabase && *connectedDatabase) hostInfoValue += std::string("/") + connectedDatabase;
    serverVersionValue = static_cast<unsigned int>(PQserverVersion(connectionHandle));
    const char *serverVersionText = PQparameterStatus(connectionHandle, "server_version");
    serverInfoValue = serverVersionText && *serverVersionText ? serverVersionText : "PostgreSQL";
}
