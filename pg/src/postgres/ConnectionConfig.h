// PostgreSQL connection configuration for gmsv_pg.
#ifndef PG_CONNECTIONCONFIG_H
#define PG_CONNECTIONCONFIG_H

#include <string>
#include <utility>
#include <vector>

#include "PGException.h"

enum SSLMode {
    SSL_MODE_DISABLED = 0,
    SSL_MODE_PREFERRED = 1,
    SSL_MODE_REQUIRED = 2,
    SSL_MODE_VERIFY_CA = 3,
    SSL_MODE_VERIFY_IDENTITY = 4,
};

struct SSLSettings {
    std::string key;
    std::string cert;
    std::string ca;
    std::string capath;
    std::string cipher;
};

// Owns durable connection options and builds libpq conninfo strings.
class ConnectionConfig {
public:
    static ConnectionConfig positional(std::string host, std::string username, std::string password,
                                       std::string database, unsigned int port, std::string unixSocket);
    static ConnectionConfig rawConnectionString(std::string connectionString);
    static ConnectionConfig options(std::vector<std::pair<std::string, std::string>> options);

    void setConnectTimeout(unsigned int timeout);
    void setSSLMode(SSLMode mode);
    void setSSLSettings(const SSLSettings &settings);

    std::string buildConnectionString() const;

private:
    static std::string quoteConninfoValue(const std::string &value);
    static bool isSafeOptionKey(const std::string &key);
    static bool containsNul(const std::string &value);
    void ensureCanApplyDerivedOption(const char *optionName) const;

    std::string database;
    std::string host;
    std::string username;
    std::string password;
    std::string unixSocket;
    unsigned int port = 0;
    bool useRawConnectionString = false;
    std::string rawConnectionStringValue;
    std::vector<std::pair<std::string, std::string>> optionTable;
    bool hasSSLMode = false;
    SSLMode sslMode = SSL_MODE_PREFERRED;
    SSLSettings sslSettings;
    unsigned int connectTimeout = 10;
};

#endif
