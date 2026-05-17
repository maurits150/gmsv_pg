// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.

#ifndef PG_LUAIQUERY_H
#define PG_LUAIQUERY_H

#include "LuaObject.h"
#include "../postgres/IQuery.h"
#include "../postgres/Query.h"
#include "../postgres/PreparedQuery.h"
#include "../postgres/Transaction.h"

#include <utility>

// Lua owns callback/table references for a single backend query execution.
class LuaCallbackReferences {
public:
    virtual ~LuaCallbackReferences() = default;

    int successReference = 0;
    int errorReference = 0;
    int abortReference = 0;
    int onDataReference = 0;
    int tableReference = 0;
};

class LuaQueryData final : public QueryData, public LuaCallbackReferences {
public:
    LuaQueryData() = default;
};

class LuaPreparedQueryData final : public PreparedQueryData, public LuaCallbackReferences {
public:
    explicit LuaPreparedQueryData(PreparedParameterMap parameters) : PreparedQueryData(std::move(parameters)) {}
};

class LuaTransactionData final : public TransactionData, public LuaCallbackReferences {
public:
    explicit LuaTransactionData(std::deque<std::pair<std::shared_ptr<Query>, std::shared_ptr<IQueryData>>> queries)
            : TransactionData(std::move(queries)) {}
};

class LuaIQuery : public LuaObject {
public:
    static void addMetaTableFunctions(ILuaBase *lua);

    std::shared_ptr<IQuery> m_query;
    //Used to ensure that the database stays alive while the query exists.
    //This needs to be the case because we can do qu:start() on the query even if the database
    //has already gone out of scope in lua. Also, the callbacks need to be run using the Database's think hook
    //even after the database is out of scope in lua code.
    int m_databaseReference = 0;

    //The table is at the top
    virtual std::shared_ptr<IQueryData> buildQueryData(ILuaBase *LUA, int stackPosition, bool shouldRef) = 0;

    static void referenceCallbacks(ILuaBase *LUA, int stackPosition, IQueryData &data);

    static LuaCallbackReferences *getCallbackReferences(const std::shared_ptr<IQueryData> &data);

    static void finishLuaQueryData(ILuaBase *LUA, const std::shared_ptr<IQuery> &query,
                                   const std::shared_ptr<IQueryData> &data);

    static void runAbortedCallback(ILuaBase *LUA, const std::shared_ptr<IQueryData> &data);

    static void runErrorCallback(ILuaBase *LUA, const std::shared_ptr<IQuery> &iQuery, const std::shared_ptr<IQueryData> &data);

    static void runCallback(ILuaBase *LUA, const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);

    void onDestroyedByLua(ILuaBase *LUA) override;

    explicit LuaIQuery(std::shared_ptr<IQuery> query, std::string className, int databaseRef) : LuaObject(
            std::move(className)), m_query(std::move(query)), m_databaseReference(databaseRef) {}
};

#endif //PG_LUAIQUERY_H
