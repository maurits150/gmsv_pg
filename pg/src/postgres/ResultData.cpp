// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "ResultData.h"

ResultDataRow::ResultDataRow(PGresult *result, int rowIndex, const std::vector<int> &columnTypes) {
    int columnCount = PQnfields(result);
    for (int index = 0; index < columnCount; ++index) {
        bool isNull = PQgetisnull(result, rowIndex, index) != 0;
        nullFields.push_back(isNull);
        if (isNull) {
            values.emplace_back();
        } else if (index < static_cast<int>(columnTypes.size()) && columnTypes[index] == PG_FIELD_BINARY) {
            size_t binaryLength = 0;
            unsigned char *binary = PQunescapeBytea(reinterpret_cast<const unsigned char *>(PQgetvalue(result, rowIndex, index)), &binaryLength);
            if (binary) {
                values.emplace_back(reinterpret_cast<const char *>(binary), binaryLength);
                PQfreemem(binary);
            } else {
                values.emplace_back();
            }
        } else {
            values.emplace_back(PQgetvalue(result, rowIndex, index), PQgetlength(result, rowIndex, index));
        }
    }
}

ResultData::ResultData() = default;

ResultData::ResultData(PGresult *result) {
    int columnCount = PQnfields(result);
    for (int i = 0; i < columnCount; ++i) {
        const char *name = PQfname(result, i);
        columns.emplace_back(name ? name : "");
        columnTypes.push_back(typeForOid(PQftype(result, i)));
    }

    int rowCount = PQntuples(result);
    for (int i = 0; i < rowCount; ++i) {
        rows.emplace_back(result, i, columnTypes);
    }
}

int ResultData::typeForOid(Oid oid) {
    switch (oid) {
        case 16:   // bool
            return PG_FIELD_BOOL;
        case 21:   // int2
        case 23:   // int4
        case 700:  // float4
        case 701:  // float8
            return PG_FIELD_NUMBER;
        case 17:   // bytea
            return PG_FIELD_BINARY;
        default:
            return PG_FIELD_STRING;
    }
}
