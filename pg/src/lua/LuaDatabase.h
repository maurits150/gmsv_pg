// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.

#ifndef PG_LUADATABASE_H
#define PG_LUADATABASE_H


#include "../postgres/Database.h"

#include <utility>
#include "LuaObject.h"

class LuaDatabase : public LuaObject {
public:
    static void createMetaTable(ILuaBase *LUA);

    static int create(lua_State *L);

    void think(ILuaBase *LUA);

    int m_tableReference = 0;
    bool m_hasOnDisconnected = false;
    bool m_hasReconnectCallbacks = false;
    std::shared_ptr<Database> m_database;
    bool m_dbCallbackRan = false;

    void onDestroyedByLua(ILuaBase *LUA) override;

    explicit LuaDatabase(std::shared_ptr<Database> database) : LuaObject("Database"),
                                                               m_database(std::move(database)) {
    }

    static void createWeakTable(ILuaBase *LUA);
    static void runAllThinkHooks(ILuaBase *LUA);
    static void shutdownAll(ILuaBase *LUA);
};


#endif //PG_LUADATABASE_H
