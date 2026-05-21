// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#ifndef PG_RESULTDATA_H
#define PG_RESULTDATA_H

#include <string>
#include <vector>

#include <libpq-fe.h>

enum PgFieldType {
    PG_FIELD_STRING = 0,
    PG_FIELD_NUMBER = 1,
    PG_FIELD_BOOL = 2,
    PG_FIELD_NULL = 3,
    PG_FIELD_BINARY = 4,
};

class ResultDataRow {
public:
    ResultDataRow(PGresult *result, int rowIndex, const std::vector<int> &columnTypes);

    std::vector<std::string> &getValues() { return values; }
    bool isFieldNull(unsigned int index) { return nullFields[index]; }

private:
    std::vector<bool> nullFields;
    std::vector<std::string> values;
};

class ResultData {
public:
    ResultData();
    explicit ResultData(PGresult *result);

    std::vector<std::string> &getColumns() { return columns; }
    std::vector<ResultDataRow> &getRows() { return rows; }
    std::vector<int> &getColumnTypes() { return columnTypes; }

private:
    static int typeForOid(Oid oid);

    std::vector<std::string> columns;
    std::vector<int> columnTypes;
    std::vector<ResultDataRow> rows;
};

// One PostgreSQL protocol result in a possibly multi-statement result chain.
struct StatementResult {
    ResultData rows;
    unsigned long long affectedRows = 0;
    unsigned long long oid = 0;
    std::string commandStatus;
};

#endif
