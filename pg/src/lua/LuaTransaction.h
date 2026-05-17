// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.
#ifndef PG_LUATRANSACTION_H
#define PG_LUATRANSACTION_H

#include "LuaIQuery.h"
#include "../postgres/Transaction.h"

class LuaTransaction : public LuaIQuery {
public:
    struct AddedQuery {
        int tableReference = 0;
        std::shared_ptr<QueryData> data;

        AddedQuery(int tableReference, std::shared_ptr<QueryData> data)
                : tableReference(tableReference), data(std::move(data)) {}
    };

    // Authoritative transaction query list. getQueries() exposes a Lua snapshot of these refs.
    std::deque<AddedQuery> m_addedQueries = {};

    std::shared_ptr<IQueryData> buildQueryData(ILuaBase *LUA, int stackPosition, bool shouldRef) override;

    void clearAddedQueries(ILuaBase *LUA);

    void onDestroyedByLua(ILuaBase *LUA) override;

    static void createMetaTable(ILuaBase *LUA);


    explicit LuaTransaction(const std::shared_ptr<Transaction> &transaction, int databaseRef) : LuaIQuery(
        std::static_pointer_cast<IQuery>(transaction), "PG Transaction", databaseRef
    ) {
    }

    static void runSuccessCallback(ILuaBase *LUA, const std::shared_ptr<Transaction> &transaction,
                                   const std::shared_ptr<TransactionData> &data);

    static void runErrorCallback(ILuaBase *LUA, const std::shared_ptr<Transaction> &transaction,
                                 const std::shared_ptr<TransactionData> &data);

    static void runAbortedCallback(ILuaBase *LUA, const std::shared_ptr<Transaction> &transaction,
                                   const std::shared_ptr<TransactionData> &data);
};


#endif //PG_LUATRANSACTION_H
