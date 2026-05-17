// PostgreSQL connection configuration for gmsv_pg.
#include "ConnectionConfig.h"

#include <sstream>

ConnectionConfig ConnectionConfig::positional(std::string host, std::string username, std::string password,
                                             std::string database, unsigned int port, std::string unixSocket) {
    ConnectionConfig config;
    config.host = std::move(host);
    config.username = std::move(username);
    config.password = std::move(password);
    config.database = std::move(database);
    config.port = port;
    config.unixSocket = std::move(unixSocket);
    return config;
}

ConnectionConfig ConnectionConfig::rawConnectionString(std::string connectionString) {
    ConnectionConfig config;
    config.useRawConnectionString = true;
    config.rawConnectionStringValue = std::move(connectionString);
    return config;
}

ConnectionConfig ConnectionConfig::options(std::vector<std::pair<std::string, std::string>> options) {
    ConnectionConfig config;
    config.optionTable = std::move(options);
    return config;
}

void ConnectionConfig::setConnectTimeout(unsigned int timeout) { connectTimeout = timeout; }

void ConnectionConfig::setSSLMode(SSLMode mode) { hasSSLMode = true; sslMode = mode; }

void ConnectionConfig::setSSLSettings(const SSLSettings &settings) {
    if (!settings.capath.empty() || !settings.cipher.empty()) {
        throw PGException("pg: capath and cipher SSL settings do not have exact libpq equivalents");
    }
    sslSettings = settings;
}

std::string ConnectionConfig::quoteConninfoValue(const std::string &value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\\' || ch == '\'') out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string ConnectionConfig::buildConnectionString() const {
    if (useRawConnectionString) return rawConnectionStringValue;

    std::ostringstream ss;
    auto add = [&](const std::string &key, const std::string &value) {
        if (!value.empty()) ss << key << "=" << quoteConninfoValue(value) << " ";
    };
    add("host", unixSocket.empty() ? host : unixSocket);
    add("user", username);
    add("password", password);
    add("dbname", database);
    if (port != 0) ss << "port=" << port << " ";
    if (connectTimeout > 0) ss << "connect_timeout=" << connectTimeout << " ";
    if (hasSSLMode) {
        switch (sslMode) {
            case SSL_MODE_DISABLED: ss << "sslmode=disable "; break;
            case SSL_MODE_PREFERRED: ss << "sslmode=prefer "; break;
            case SSL_MODE_REQUIRED: ss << "sslmode=require "; break;
            case SSL_MODE_VERIFY_CA: ss << "sslmode=verify-ca "; break;
            case SSL_MODE_VERIFY_IDENTITY: ss << "sslmode=verify-full "; break;
        }
    }
    add("sslkey", sslSettings.key);
    add("sslcert", sslSettings.cert);
    add("sslrootcert", sslSettings.ca);
    for (const auto &option : optionTable) add(option.first, option.second);
    return ss.str();
}
