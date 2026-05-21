// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.

#include "LuaQuery.h"

#include <locale>
#include <sstream>

static double postgresNumberToLua(const std::string &value) {
    std::istringstream stream(value);
    stream.imbue(std::locale::classic());
    double number = 0;
    stream >> number;
    return number;
}

static std::shared_ptr<Query> getBackendQuery(ILuaBase *LUA) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = std::dynamic_pointer_cast<Query>(luaQuery->m_query);
    if (!query) {
        LUA->ThrowError("[PG] Expected PG Query backend");
        throw PGException("[PG] Expected PG Query backend");
    }
    return query;
}

//Function that converts PostgreSQL result data into a lua type.
//Expects the row table to be at the top of the stack at the start of this function
//Adds a column to the row table
static void dataToLua(const QueryData &data,
                      GarrysMod::Lua::ILuaBase *LUA, unsigned int column,
                      std::string &columnValue, const char *columnName, int columnType, bool isNull) {
    bool useNumericField = data.hasOption(OPTION_NUMERIC_FIELDS) || !data.hasOption(OPTION_NAMED_FIELDS);
    bool interpretData = data.hasOption(OPTION_INTERPRET_DATA);
    if (useNumericField) {
        LUA->PushNumber(column);
    }
    if (isNull) {
        LUA->PushNil();
    } else if (!interpretData) {
        LUA->PushString(columnValue.c_str(), (unsigned int) columnValue.length());
    } else {
        switch (columnType) {
            case PG_FIELD_NUMBER:
                LUA->PushNumber(postgresNumberToLua(columnValue));
                break;
            case PG_FIELD_BOOL: {
                LUA->PushBool(columnValue == "t" || columnValue == "true" || columnValue == "1");
                break;
            }
            case PG_FIELD_NULL:
                LUA->PushNil();
                break;
            default:
                LUA->PushString(columnValue.c_str(), (unsigned int) columnValue.length());
                break;
        }
    }
    if (useNumericField) {
        LUA->SetTable(-3);
    } else {
        LUA->SetField(-2, columnName);
    }
}

// Builds a temporary Lua table reference for the current result set of the query.
int LuaQuery::createResultTableReference(GarrysMod::Lua::ILuaBase *LUA, Query &query, QueryData &data) {
    LUA->CreateTable();
    int dataStackPosition = LUA->Top();
    if (query.hasCallbackData() && data.hasAnyResults()) {
        ResultData &currentData = data.getResult();
        for (unsigned int i = 0; i < currentData.getRows().size(); i++) {
            ResultDataRow &row = currentData.getRows()[i];
            LUA->CreateTable();
            int rowStackPosition = LUA->Top();
            for (unsigned int j = 0; j < row.getValues().size(); j++) {
                dataToLua(data, LUA, j + 1, row.getValues()[j], currentData.getColumns()[j].c_str(),
                          currentData.getColumnTypes()[j], row.isFieldNull(j));
            }
            LUA->Push(dataStackPosition);
            LUA->PushNumber(i + 1);
            LUA->Push(rowStackPosition);
            LUA->SetTable(-3);
            LUA->Pop(2); //data + row
        }
    }
    return LuaReferenceCreate(LUA);
}

static void runOnDataCallbacks(
        ILuaBase *LUA,
        const std::shared_ptr<Query> &query,
        const std::shared_ptr<IQueryData> &data,
        int dataReference
) {
    auto refs = LuaIQuery::getCallbackReferences(data);
    if (!refs || refs->tableReference == 0) return;

    if (!LuaIQuery::pushCallbackReference(LUA, refs->onDataReference, refs->tableReference,
                                          "onData", data->isFirstData())) {
        return;
    }
    int callbackPosition = LUA->Top();
    int index = 1;
    LUA->ReferencePush(dataReference);
    while (true) {
        LUA->PushNumber(index++);
        LUA->RawGet(-2);
        if (LUA->GetType(-1) == GarrysMod::Lua::Type::Nil) {
            LUA->Pop(); //Nil
            break;
        }
        int rowPosition = LUA->Top();
        LUA->Push(callbackPosition);
        LUA->ReferencePush(refs->tableReference);
        LUA->Push(rowPosition);
        LuaObject::pcallWithErrorReporter(LUA, 2);

        LUA->Pop(); //Row
    }

    LUA->Pop(2); //Callback, data
}


void LuaQuery::runSuccessCallback(ILuaBase *LUA, const std::shared_ptr<Query>& query, const std::shared_ptr<QueryData> &data) {
    int dataReference = LuaQuery::createResultTableReference(LUA, *query, *data);
    runOnDataCallbacks(LUA, query, data, dataReference);

    auto refs = LuaIQuery::getCallbackReferences(data);
    if (!refs || !LuaIQuery::pushCallbackReference(LUA, refs->successReference, refs->tableReference,
                                          "onSuccess", data->isFirstData())) {
        LuaReferenceFree(LUA, dataReference);
        return;
    }
    LUA->ReferencePush(refs->tableReference);
    LUA->ReferencePush(dataReference);
    LuaObject::pcallWithErrorReporter(LUA, 2);
    LuaReferenceFree(LUA, dataReference);
}

PG_LUA_FUNCTION(affectedRows) {
    auto query = getBackendQuery(LUA);
    LUA->PushNumber((double) query->affectedRows());
    return 1;
}

PG_LUA_FUNCTION(commandStatus) {
    auto query = getBackendQuery(LUA);
    auto status = query->commandStatus();
    LUA->PushString(status.c_str());
    return 1;
}

PG_LUA_FUNCTION(oid) {
    auto query = getBackendQuery(LUA);
    LUA->PushNumber((double) query->oid());
    return 1;
}

PG_LUA_FUNCTION(lastInsert) {
    throw PGException("pg: lastInsert() is MySQL-specific and is not supported; use INSERT ... RETURNING instead");
}

PG_LUA_FUNCTION(getData) {
    auto query = getBackendQuery(LUA);
    auto data = std::dynamic_pointer_cast<QueryData>(query->getCallbackData());
    if (!query->hasCallbackData() || !data || data->getResultStatus() == QUERY_ERROR) {
        LUA->PushNil();
    } else {
        int ref = LuaQuery::createResultTableReference(LUA, *query, *data);
        LUA->ReferencePush(ref);
        LuaReferenceFree(LUA, ref);
    }
    return 1;
}

PG_LUA_FUNCTION(hasMoreResults) {
    auto query = getBackendQuery(LUA);
    auto data = std::dynamic_pointer_cast<QueryData>(query->getCallbackData());
    LUA->PushBool(data && data->hasMoreResults());
    return 1;
}

PG_LUA_FUNCTION(getNextResults) {
    auto query = getBackendQuery(LUA);
    auto data = std::dynamic_pointer_cast<QueryData>(query->getCallbackData());
    if (!query->hasCallbackData() || !data || !data->advanceResult()) {
        LUA->PushNil();
        return 1;
    }
    int ref = LuaQuery::createResultTableReference(LUA, *query, *data);
    LUA->ReferencePush(ref);
    LuaReferenceFree(LUA, ref);
    return 1;
}

void LuaQuery::addMetaTableFunctions(ILuaBase *LUA) {
    LuaIQuery::addMetaTableFunctions(LUA);

    LUA->PushCFunction(affectedRows);
    LUA->SetField(-2, "affectedRows");
    LUA->PushCFunction(commandStatus);
    LUA->SetField(-2, "commandStatus");
    LUA->PushCFunction(oid);
    LUA->SetField(-2, "oid");
    LUA->PushCFunction(lastInsert);
    LUA->SetField(-2, "lastInsert");
    LUA->PushCFunction(getData);
    LUA->SetField(-2, "getData");
    LUA->PushCFunction(hasMoreResults);
    LUA->SetField(-2, "hasMoreResults");
    LUA->PushCFunction(getNextResults);
    LUA->SetField(-2, "getNextResults");
}

void LuaQuery::createMetaTable(ILuaBase *LUA) {
    LuaIQuery::TYPE_QUERY = LUA->CreateMetaTable("PG Query");
    LuaQuery::addMetaTableFunctions(LUA);
    LUA->Pop(); //Metatable
}

std::shared_ptr<IQueryData> LuaQuery::buildQueryData(ILuaBase *LUA, int stackPosition, bool shouldRef) {
    std::shared_ptr<QueryData> data(new LuaQueryData());
    auto backendQuery = std::dynamic_pointer_cast<Query>(m_query);
    if (!backendQuery) throw PGException("[PG] Expected PG Query backend");
    backendQuery->snapshotOptions(data);
    data->setMultiStatementsEnabled(backendQuery->multiStatementsForNewExecution());
    data->setStatus(QUERY_COMPLETE);
    if (shouldRef) {
        LuaIQuery::referenceCallbacks(LUA, stackPosition, *data);
    }
    return data;
}

void LuaQuery::onDestroyedByLua(ILuaBase *LUA) {
    LuaIQuery::onDestroyedByLua(LUA);
}
