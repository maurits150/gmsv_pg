// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.
#include "GarrysMod/Lua/Interface.h"
#include "../postgres/Database.h"
#include "LuaObject.h"
#include "LuaDatabase.h"
#include "LuaTransaction.h"
#include "LuaQuery.h"
#include "LuaPreparedQuery.h"

#define PG_VERSION "1"
#define PG_MINOR_VERSION "0"

GMOD_MODULE_CLOSE() {
    LuaDatabase::shutdownAll(LUA);

    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "hook");
    if (LUA->IsType(-1, GarrysMod::Lua::Type::Table)) {
        LUA->GetField(-1, "Remove");
        if (LUA->IsType(-1, GarrysMod::Lua::Type::Function)) {
            LUA->PushString("Think");
            LUA->PushString("__PGThinkHook");
            LUA->Call(2, 0);
        } else {
            LUA->Pop();
        }
    }
    LUA->PushNil();
    LUA->SetField(-3, "pg");
    LUA->Pop(2); // hook, global
    return 0;
}

/* Returns the amount of currently allocated objects.
 */
LUA_FUNCTION(objectCount) {
    LUA->PushNumber((double) (LuaObject::allocationCount - LuaObject::deallocationCount));
    return 1;
}

LUA_FUNCTION(allocationCount) {
    LUA->PushNumber(LuaObject::allocationCount);
    return 1;
}

LUA_FUNCTION(deallocationCount) {
    LUA->PushNumber(LuaObject::deallocationCount);
    return 1;
}

LUA_FUNCTION(referenceCreatedCount) {
    LUA->PushNumber((double) LuaObject::referenceCreatedCount.load());
    return 1;
}

LUA_FUNCTION(referenceFreedCount) {
    LUA->PushNumber((double) LuaObject::referenceFreedCount.load());
    return 1;
}

LUA_FUNCTION(pgThink) {
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "pg");
    if (LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) {
        LUA->Pop(2); //nil, Global
        return 0;
    }
    LuaDatabase::runAllThinkHooks(LUA);
    LUA->Pop(2); //nil, Global
    return 0;
}

GMOD_MODULE_OPEN() {
    //Creating MetaTables
    LuaObject::createUserDataMetaTable(LUA);
    LuaDatabase::createMetaTable(LUA);
    LuaQuery::createMetaTable(LUA);
    LuaPreparedQuery::createMetaTable(LUA);
    LuaTransaction::createMetaTable(LUA);

    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "hook");
    if (LUA->IsType(-1, GarrysMod::Lua::Type::Table)) {
        LUA->GetField(-1, "Add");
        if (LUA->IsType(-1, GarrysMod::Lua::Type::Function)) {
            LUA->PushString("Think");
            LUA->PushString("__PGThinkHook");
            LUA->PushCFunction(pgThink);
            LUA->Call(3, 0);
        } else {
            LUA->Pop();
        }
    }
    LUA->Pop(2); // hook, global
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->CreateTable(); //pg

    LUA->PushString(PG_VERSION);
    LUA->SetField(-2, "VERSION");
    LUA->PushString(PG_MINOR_VERSION);
    LUA->SetField(-2, "MINOR_VERSION");

    LUA->PushNumber(DATABASE_CONNECTED);
    LUA->SetField(-2, "DATABASE_CONNECTED");
    LUA->PushNumber(DATABASE_CONNECTING);
    LUA->SetField(-2, "DATABASE_CONNECTING");
    LUA->PushNumber(DATABASE_NOT_CONNECTED);
    LUA->SetField(-2, "DATABASE_NOT_CONNECTED");
    LUA->PushNumber(DATABASE_CONNECTION_FAILED);
    LUA->SetField(-2, "DATABASE_CONNECTION_FAILED");

    LUA->PushNumber(QUERY_NOT_RUNNING);
    LUA->SetField(-2, "QUERY_NOT_RUNNING");
    LUA->PushNumber(QUERY_RUNNING);
    LUA->SetField(-2, "QUERY_RUNNING");
    LUA->PushNumber(QUERY_COMPLETE);
    LUA->SetField(-2, "QUERY_COMPLETE");
    LUA->PushNumber(QUERY_ABORTED);
    LUA->SetField(-2, "QUERY_ABORTED");
    LUA->PushNumber(QUERY_WAITING);
    LUA->SetField(-2, "QUERY_WAITING");

    LUA->PushNumber(OPTION_NUMERIC_FIELDS);
    LUA->SetField(-2, "OPTION_NUMERIC_FIELDS");
    LUA->PushNumber(OPTION_INTERPRET_DATA);
    LUA->SetField(-2, "OPTION_INTERPRET_DATA"); //Compatibility constant; supported types are interpreted by default.
    LUA->PushNumber(OPTION_NAMED_FIELDS);
    LUA->SetField(-2, "OPTION_NAMED_FIELDS"); //Compatibility constant; named fields are the default.
    LUA->PushNumber(OPTION_CACHE);
    LUA->SetField(-2, "OPTION_CACHE"); //Compatibility constant; prepared cache is unsupported.

    LUA->PushNumber(SSL_MODE_DISABLED);
    LUA->SetField(-2, "SSL_MODE_DISABLED");
    LUA->PushNumber(SSL_MODE_PREFERRED);
    LUA->SetField(-2, "SSL_MODE_PREFERRED");
    LUA->PushNumber(SSL_MODE_REQUIRED);
    LUA->SetField(-2, "SSL_MODE_REQUIRED");
    LUA->PushNumber(SSL_MODE_VERIFY_CA);
    LUA->SetField(-2, "SSL_MODE_VERIFY_CA");
    LUA->PushNumber(SSL_MODE_VERIFY_IDENTITY);
    LUA->SetField(-2, "SSL_MODE_VERIFY_IDENTITY");

    LUA->PushCFunction(LuaDatabase::create);
    LUA->SetField(-2, "connect");
    LUA->PushCFunction(LuaDatabase::create);
    LUA->SetField(-2, "new_connection");

    //Debug/testing functions
    LUA->PushCFunction(objectCount);
    LUA->SetField(-2, "objectCount");
    LUA->PushCFunction(allocationCount);
    LUA->SetField(-2, "allocationCount");
    LUA->PushCFunction(deallocationCount);
    LUA->SetField(-2, "deallocationCount");
    LUA->PushCFunction(referenceFreedCount);
    LUA->SetField(-2, "referenceFreedCount");
    LUA->PushCFunction(referenceCreatedCount);
    LUA->SetField(-2, "referenceCreatedCount");


    LuaDatabase::createWeakTable(LUA);

    LUA->SetField(-2, "pg");
    LUA->Pop();

    return 0;
}
