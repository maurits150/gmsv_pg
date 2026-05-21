// PostgreSQL connection configuration for gmsv_pg.
#include "ConnectionConfig.h"

#include <cctype>
#include <sstream>

ConnectionConfig ConnectionConfig::positional(std::string host, std::string username, std::string password,
                                              std::string database, unsigned int port, std::string unixSocket) {
    if (containsNul(host) || containsNul(username) || containsNul(password) ||
        containsNul(database) || containsNul(unixSocket)) {
        throw PGException("pg: connection parameters cannot contain embedded NUL bytes");
    }
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
    if (containsNul(connectionString)) throw PGException("pg: connection string cannot contain embedded NUL bytes");
    ConnectionConfig config;
    config.useRawConnectionString = true;
    config.rawConnectionStringValue = std::move(connectionString);
    return config;
}

ConnectionConfig ConnectionConfig::options(std::vector<std::pair<std::string, std::string>> options) {
    ConnectionConfig config;
    for (const auto &option : options) {
        if (!isSafeOptionKey(option.first)) {
            throw PGException("pg: invalid libpq option key in connection option table");
        }
        if (containsNul(option.second)) {
            throw PGException("pg: libpq option values cannot contain embedded NUL bytes");
        }
    }
    config.optionTable = std::move(options);
    return config;
}

void ConnectionConfig::setConnectTimeout(unsigned int timeout) {
    ensureCanApplyDerivedOption("setConnectTimeout");
    connectTimeout = timeout;
}

void ConnectionConfig::setSSLMode(SSLMode mode) {
    ensureCanApplyDerivedOption("setSSLMode");
    hasSSLMode = true;
    sslMode = mode;
}

void ConnectionConfig::setSSLSettings(const SSLSettings &settings) {
    ensureCanApplyDerivedOption("setSSLSettings");
    if (!settings.capath.empty() || !settings.cipher.empty()) {
        throw PGException("pg: capath and cipher SSL settings do not have exact libpq equivalents");
    }
    if (containsNul(settings.key) || containsNul(settings.cert) || containsNul(settings.ca)) {
        throw PGException("pg: SSL settings cannot contain embedded NUL bytes");
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

bool ConnectionConfig::isSafeOptionKey(const std::string &key) {
    if (key.empty()) return false;
    for (char ch : key) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || ch == '_')) return false;
    }
    return true;
}

bool ConnectionConfig::containsNul(const std::string &value) {
    return value.find('\0') != std::string::npos;
}

void ConnectionConfig::ensureCanApplyDerivedOption(const char *optionName) const {
    if (useRawConnectionString) {
        throw PGException(std::string("pg: ") + optionName + "() cannot modify a raw connection string; put the option in the connection string");
    }
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
    for (const auto &option : optionTable) add(option.first, option.second);
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
    return ss.str();
}
