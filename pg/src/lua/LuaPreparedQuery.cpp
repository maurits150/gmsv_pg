// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.
#include "LuaPreparedQuery.h"

#include <cmath>
#include <limits>

static unsigned int getParameterIndex(ILuaBase *LUA, int stackPosition) {
    LUA->CheckType(stackPosition, GarrysMod::Lua::Type::Number);
    double index = LUA->GetNumber(stackPosition);
    if (index < 1 || std::floor(index) != index || index > std::numeric_limits<unsigned int>::max()) {
        throw PGException("Index must be greater than 0 and an integer");
    }
    return static_cast<unsigned int>(index);
}

PG_LUA_FUNCTION(setNumber) {
    auto luaQuery = LuaObject::getLuaObject<LuaPreparedQuery>(LUA);
    auto query = (PreparedQuery *) luaQuery->m_query.get();
    LUA->CheckType(3, GarrysMod::Lua::Type::Number);
    auto uIndex = getParameterIndex(LUA, 2);
    double value = LUA->GetNumber(3);

    query->setNumber(uIndex, value);
    return 0;
}

PG_LUA_FUNCTION(setString) {
    auto luaQuery = LuaObject::getLuaObject<LuaPreparedQuery>(LUA);
    auto query = (PreparedQuery *) luaQuery->m_query.get();
    LUA->CheckType(3, GarrysMod::Lua::Type::String);
    unsigned int length = 0;
    const char *string = LUA->GetString(3, &length);
    auto uIndex = getParameterIndex(LUA, 2);
    query->setString(uIndex, std::string(string, length));
    return 0;
}

PG_LUA_FUNCTION(setBoolean) {
    auto luaQuery = LuaObject::getLuaObject<LuaPreparedQuery>(LUA);
    auto query = (PreparedQuery *) luaQuery->m_query.get();
    LUA->CheckType(3, GarrysMod::Lua::Type::Bool);
    auto uIndex = getParameterIndex(LUA, 2);
    bool value = LUA->GetBool(3);
    query->setBoolean(uIndex, value);
    return 0;
}

PG_LUA_FUNCTION(setNull) {
    auto luaQuery = LuaObject::getLuaObject<LuaPreparedQuery>(LUA);
    auto query = (PreparedQuery *) luaQuery->m_query.get();
    auto uIndex = getParameterIndex(LUA, 2);
    query->setNull(uIndex);
    return 0;
}

PG_LUA_FUNCTION(putNewParameters) {
    auto luaQuery = LuaObject::getLuaObject<LuaPreparedQuery>(LUA);
    auto query = (PreparedQuery *) luaQuery->m_query.get();
    query->putNewParameters();
    return 0;
}

PG_LUA_FUNCTION(clearParameters) {
    auto luaQuery = LuaObject::getLuaObject<LuaPreparedQuery>(LUA);
    auto query = (PreparedQuery *) luaQuery->m_query.get();
    query->clearParameters();
    return 0;
}

void LuaPreparedQuery::createMetaTable(ILuaBase *LUA) {
    LuaIQuery::TYPE_PREPARED_QUERY = LUA->CreateMetaTable("PG Prepared Query");
    LuaQuery::addMetaTableFunctions(LUA);

    LUA->PushCFunction(setNumber);
    LUA->SetField(-2, "setNumber");
    LUA->PushCFunction(setString);
    LUA->SetField(-2, "setString");
    LUA->PushCFunction(setBoolean);
    LUA->SetField(-2, "setBoolean");
    LUA->PushCFunction(setNull);
    LUA->SetField(-2, "setNull");
    LUA->PushCFunction(putNewParameters);
    LUA->SetField(-2, "putNewParameters");
    LUA->PushCFunction(clearParameters);
    LUA->SetField(-2, "clearParameters");

    LUA->Pop(); //Metatable
}

std::shared_ptr<IQueryData> LuaPreparedQuery::buildQueryData(ILuaBase* LUA, int stackPosition, bool shouldRef) {
    auto query = (PreparedQuery*) m_query.get();
    std::shared_ptr<QueryData> data(new LuaPreparedQueryData(query->snapshotParameters()));
    if (shouldRef) {
        LuaIQuery::referenceCallbacks(LUA, stackPosition, *data);
    }
    return data;
}
