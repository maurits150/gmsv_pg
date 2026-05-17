// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.

#ifndef PG_LUAQUERY_H
#define PG_LUAQUERY_H


#include "LuaIQuery.h"

class LuaQuery : public LuaIQuery {
public:
    static int createDataReference(ILuaBase *LUA, Query &query, QueryData &data);

    static void addMetaTableFunctions(ILuaBase *LUA);

    static void createMetaTable(ILuaBase *LUA);

    static void runSuccessCallback(ILuaBase *LUA, const std::shared_ptr<Query>& query, const std::shared_ptr<QueryData> &data);

    std::shared_ptr<IQueryData> buildQueryData(ILuaBase *LUA, int stackPosition, bool shouldRef) override;

    void onDestroyedByLua(ILuaBase *LUA) override;

protected:
    explicit LuaQuery(const std::shared_ptr<Query> &query, const std::string &className, int databaseRef) : LuaIQuery(
            std::dynamic_pointer_cast<IQuery>(query), className, databaseRef) {
    }

public:
    explicit LuaQuery(const std::shared_ptr<Query> &query, int databaseRef) : LuaQuery(query, "PG Query",
                                                                                       databaseRef) {

    }
};

#endif //PG_LUAQUERY_H
