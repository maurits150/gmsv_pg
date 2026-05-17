// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.

#include "LuaQuery.h"

//Function that converts PostgreSQL result data into a lua type.
//Expects the row table to be at the top of the stack at the start of this function
//Adds a column to the row table
static void dataToLua(Query &query,
                      GarrysMod::Lua::ILuaBase *LUA, unsigned int column,
                      std::string &columnValue, const char *columnName, int columnType, bool isNull) {
    if (query.hasOption(OPTION_NUMERIC_FIELDS)) {
        LUA->PushNumber(column);
    }
    if (isNull) {
        LUA->PushNil();
    } else {
        switch (columnType) {
            case PG_FIELD_NUMBER:
                LUA->PushNumber(atof(columnValue.c_str()));
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
    if (query.hasOption(OPTION_NUMERIC_FIELDS)) {
        LUA->SetTable(-3);
    } else {
        LUA->SetField(-2, columnName);
    }
}

//Stores the data associated with the current result set of the query
//Only called once per result set (and then cached)
int LuaQuery::createDataReference(GarrysMod::Lua::ILuaBase *LUA, Query &query, QueryData &data) {
    LUA->CreateTable();
    int dataStackPosition = LUA->Top();
    if (query.hasCallbackData() && data.hasMoreResults()) {
        ResultData &currentData = data.getResult();
        for (unsigned int i = 0; i < currentData.getRows().size(); i++) {
            ResultDataRow &row = currentData.getRows()[i];
            LUA->CreateTable();
            int rowStackPosition = LUA->Top();
            for (unsigned int j = 0; j < row.getValues().size(); j++) {
                dataToLua(query, LUA, j + 1, row.getValues()[j], currentData.getColumns()[j].c_str(),
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
    int dataReference = LuaQuery::createDataReference(LUA, *query, *data);
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
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = (Query *) luaQuery->m_query.get();
    LUA->PushNumber((double) query->affectedRows());
    return 1;
}

PG_LUA_FUNCTION(commandStatus) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = (Query *) luaQuery->m_query.get();
    auto status = query->commandStatus();
    LUA->PushString(status.c_str());
    return 1;
}

PG_LUA_FUNCTION(oid) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = (Query *) luaQuery->m_query.get();
    LUA->PushNumber((double) query->oid());
    return 1;
}

PG_LUA_FUNCTION(lastInsert) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = (Query *) luaQuery->m_query.get();
    LUA->PushNumber((double) query->lastInsert());
    return 1;
}

PG_LUA_FUNCTION(getData) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = std::dynamic_pointer_cast<Query>(luaQuery->m_query);
    if (!query->hasCallbackData() || query->callbackQueryData->getResultStatus() == QUERY_ERROR) {
        LUA->PushNil();
    } else {
        int ref = LuaQuery::createDataReference(LUA, *query, (QueryData &) *(query->callbackQueryData));
        LUA->ReferencePush(ref);
        LuaReferenceFree(LUA, ref);
    }
    return 1;
}

PG_LUA_FUNCTION(hasMoreResults) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = (Query *) luaQuery->m_query.get();
    LUA->PushBool(query->hasMoreResults());
    return 1;
}

PG_LUA_FUNCTION(getNextResults) {
    auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA);
    auto query = std::dynamic_pointer_cast<Query>(luaQuery->m_query);
    query->getNextResults();
    return 0;
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
    auto query = std::dynamic_pointer_cast<Query>(this->m_query);
    auto data = query->buildQueryData();
    data->setStatus(QUERY_COMPLETE);
    if (shouldRef) {
        LuaIQuery::referenceCallbacks(LUA, stackPosition, *data);
    }
    return data;
}

void LuaQuery::onDestroyedByLua(ILuaBase *LUA) {
    LuaIQuery::onDestroyedByLua(LUA);
}
