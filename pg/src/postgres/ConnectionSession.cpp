// Owns a live PostgreSQL connection session for gmsv_pg.
#include "ConnectionSession.h"

#include "PGException.h"

bool ConnectionSession::connect(const ConnectionConfig &config) {
    try {
        connectionHandle.reset(new pqxx::connection(config.buildConnectionString()));
        updateMetadata();
        connectionError.clear();
        return connectionHandle->is_open();
    } catch (const std::exception &error) {
        connectionError = error.what();
        connectionHandle.reset();
        return false;
    }
}

void ConnectionSession::reset() { connectionHandle.reset(); }

bool ConnectionSession::isOpen() const { return connectionHandle && connectionHandle->is_open(); }

pqxx::connection &ConnectionSession::connection() { return *connectionHandle; }

pqxx::connection *ConnectionSession::connectionPtr() { return connectionHandle.get(); }

bool ConnectionSession::ping() {
    if (!isOpen()) return false;
    pqxx::work tx(*connectionHandle);
    tx.exec("SELECT 1");
    tx.commit();
    return true;
}

std::string ConnectionSession::escape(const std::string &str) {
    if (!isOpen()) throw PGException("Cannot escape using database that is not connected");
    return connectionHandle->esc(str);
}

bool ConnectionSession::setCharacterSet(const std::string &characterSet) {
    if (!isOpen()) throw PGException("Database needs to be connected to change charset.");
    pqxx::work tx(*connectionHandle);
    tx.exec("SET CLIENT_ENCODING TO " + tx.quote(characterSet));
    tx.commit();
    return true;
}

void ConnectionSession::updateMetadata() {
    const char *connectedHost = connectionHandle->hostname();
    const char *connectedPort = connectionHandle->port();
    const char *connectedDatabase = connectionHandle->dbname();
    hostInfoValue = connectedHost && *connectedHost ? connectedHost : "local";
    if (connectedPort && *connectedPort) hostInfoValue += std::string(":") + connectedPort;
    if (connectedDatabase && *connectedDatabase) hostInfoValue += std::string("/") + connectedDatabase;
    serverVersionValue = connectionHandle->server_version();
}
