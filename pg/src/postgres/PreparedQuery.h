// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_PREPAREDQUERY_H
#define PG_PREPAREDQUERY_H

#include <memory>
#include <string>
#include <unordered_map>

#include "Query.h"

enum PreparedFieldType {
    PREPARED_FIELD_NUMBER,
    PREPARED_FIELD_STRING,
    PREPARED_FIELD_BOOLEAN,
    PREPARED_FIELD_NULL,
};

class PreparedQueryField {
    friend class PreparedQuery;

public:
    PreparedQueryField(unsigned int index, PreparedFieldType type, std::string value = "")
        : m_index(index), m_type(type), m_value(std::move(value)) {}

private:
    unsigned int m_index;
    PreparedFieldType m_type;
    std::string m_value;
};

using PreparedParameterMap = std::unordered_map<unsigned int, std::shared_ptr<PreparedQueryField>>;

class PreparedQueryData : public QueryData {
    friend class PreparedQuery;

protected:
    explicit PreparedQueryData(PreparedParameterMap parameters = {}) : m_parameters(std::move(parameters)) {}

    PreparedParameterMap m_parameters;
};

class PreparedQuery : public Query {
    friend class Database;

public:
    ~PreparedQuery() override;

    void executeStatement(Database &database, PGconn *connection,
                          const std::shared_ptr<IQueryData> &data) override;
    void executeInTransaction(Database &database, PGconn *connection,
                              const std::shared_ptr<IQueryData> &data) override;
    void clearParameters();
    void setNumber(unsigned int index, double value);
    void setString(unsigned int index, const std::string &value);
    void setBoolean(unsigned int index, bool value);
    void setNull(unsigned int index);
    PreparedParameterMap snapshotParameters() const;

    static std::shared_ptr<PreparedQuery> create(const std::shared_ptr<Database> &database, std::string query);

private:
    PreparedQuery(const std::shared_ptr<Database> &database, std::string query);

    static unsigned int maxParameterIndex(const PreparedParameterMap &params);
    static std::string numberToString(double value);

    PreparedParameterMap m_parameters;
};

#endif
