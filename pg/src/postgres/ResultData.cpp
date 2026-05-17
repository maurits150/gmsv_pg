// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "ResultData.h"

#include <pqxx/binarystring>

ResultDataRow::ResultDataRow(const pqxx::row &row, const std::vector<int> &columnTypes) {
    unsigned int index = 0;
    for (auto field : row) {
        nullFields.push_back(field.is_null());
        if (field.is_null()) {
            values.emplace_back();
        } else if (index < columnTypes.size() && columnTypes[index] == PG_FIELD_BINARY) {
            pqxx::binarystring binary(field);
            values.emplace_back(binary.get(), binary.size());
        } else {
            values.emplace_back(field.c_str());
        }
        ++index;
    }
}

ResultData::ResultData() = default;

ResultData::ResultData(const pqxx::result &result) {
    if (!result.empty()) {
        const auto first = result.front();
        for (auto field : first) {
            columns.emplace_back(field.name());
            columnTypes.push_back(typeForOid(field.type()));
        }
    } else {
        for (pqxx::result::size_type i = 0; i < result.columns(); ++i) {
            columns.emplace_back(result.column_name(i));
            columnTypes.push_back(typeForOid(result.column_type(static_cast<int>(i))));
        }
    }

    for (auto row : result) {
        rows.emplace_back(row, columnTypes);
    }
}

int ResultData::typeForOid(pqxx::oid oid) {
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
