// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.
#include "LuaTransaction.h"
#include "LuaQuery.h"

PG_LUA_FUNCTION(addQuery) {
    auto luaTransaction = LuaObject::getLuaObject<LuaTransaction>(LUA);

    auto addedLuaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA, 2);
    if (luaTransaction->m_query->database() != addedLuaQuery->m_query->database()) {
        throw PGException("pg: transaction child query must belong to the same database");
    }
    auto queryData = std::dynamic_pointer_cast<QueryData>(addedLuaQuery->buildQueryData(LUA, 2, false));
    queryData->setStatus(QUERY_NOT_RUNNING);
    LUA->Push(2);
    int queryReference = LuaReferenceCreate(LUA);
    luaTransaction->m_addedQueries.emplace_back(queryReference, queryData);
    return 0;
}

PG_LUA_FUNCTION(getQueries) {
    auto luaTransaction = LuaObject::getLuaObject<LuaTransaction>(LUA);
    LUA->CreateTable();
    int index = 0;
    for (const auto &entry: luaTransaction->m_addedQueries) {
        LUA->PushNumber((double) (++index));
        LUA->ReferencePush(entry.tableReference);
        LUA->SetTable(-3);
    }
    return 1;
}

PG_LUA_FUNCTION(clearQueries) {
    auto luaTransaction = LuaObject::getLuaObject<LuaTransaction>(LUA);

    luaTransaction->clearAddedQueries(LUA);

    return 0;
}

void LuaTransaction::createMetaTable(ILuaBase *LUA) {
    LuaObject::TYPE_TRANSACTION = LUA->CreateMetaTable("PG Transaction");

    LuaIQuery::addMetaTableFunctions(LUA);
    LUA->PushCFunction(addQuery);
    LUA->SetField(-2, "addQuery");
    LUA->PushCFunction(getQueries);
    LUA->SetField(-2, "getQueries");
    LUA->PushCFunction(clearQueries);
    LUA->SetField(-2, "clearQueries");
    LUA->Pop();
}

std::shared_ptr<IQueryData> LuaTransaction::buildQueryData(ILuaBase *LUA, int stackPosition, bool shouldRef) {
    std::deque<std::pair<std::shared_ptr<Query>, std::shared_ptr<IQueryData>>> queries;
    for (auto &entry: this->m_addedQueries) {
        LUA->ReferencePush(entry.tableReference);
        auto luaQuery = LuaQuery::getLuaObject<LuaQuery>(LUA, -1);
        auto query = std::dynamic_pointer_cast<Query>(luaQuery->m_query);
        query->addQueryData(entry.data);
        queries.emplace_back(query, entry.data);
        LUA->Pop(); //Query
    }

    std::shared_ptr<IQueryData> data(new LuaTransactionData(queries));
    if (shouldRef) {
        LuaIQuery::referenceCallbacks(LUA, stackPosition, *data);
    }
    return data;
}

void LuaTransaction::runAbortedCallback(GarrysMod::Lua::ILuaBase *LUA, const std::shared_ptr<Transaction> &transaction,
                                       const std::shared_ptr<TransactionData> &data) {
    auto transactionData = std::dynamic_pointer_cast<TransactionData>(data);
    if (!LuaIQuery::getCallbackReferences(data)) return;
    // Set the correct callback data for the queries of the transaction
    for (auto &pair: transactionData->m_queries) {
        auto query = pair.first;
        auto queryData = std::dynamic_pointer_cast<QueryData>(pair.second);
        query->setCallbackData(pair.second);
    }
    LuaIQuery::runAbortedCallback(LUA, data);
}

void LuaTransaction::runErrorCallback(GarrysMod::Lua::ILuaBase *LUA, const std::shared_ptr<Transaction> &transaction,
                                      const std::shared_ptr<TransactionData> &data) {
    auto transactionData = std::dynamic_pointer_cast<TransactionData>(data);
    if (!LuaIQuery::getCallbackReferences(data)) return;
    // Set the correct callback data for the queries of the transaction
    for (auto &pair: transactionData->m_queries) {
        auto query = pair.first;
        auto queryData = std::dynamic_pointer_cast<QueryData>(pair.second);
        query->setCallbackData(pair.second);
    }
    auto refs = LuaIQuery::getCallbackReferences(data);
    if (!LuaIQuery::pushCallbackReference(LUA, refs->errorReference, refs->tableReference,
                                          "onError", data->isFirstData())) {
        return;
    }
    LUA->ReferencePush(refs->tableReference);
    auto error = data->getError();
    LUA->PushString(error.c_str());
    LuaObject::pcallWithErrorReporter(LUA, 2);
}

void LuaTransaction::runSuccessCallback(ILuaBase *LUA, const std::shared_ptr<Transaction> &transaction,
                                        const std::shared_ptr<TransactionData> &data) {
    auto transactionData = std::dynamic_pointer_cast<TransactionData>(data);
    auto refs = LuaIQuery::getCallbackReferences(data);
    if (!refs || refs->tableReference == 0) return;
    transactionData->setStatus(QUERY_COMPLETE);
    LUA->CreateTable();
    int index = 0;
    // Set the correct callback data for the queries of the transaction
    for (auto &pair: transactionData->m_queries) {
        LUA->PushNumber((double) (++index));
        auto query = pair.first;
        auto queryData = std::dynamic_pointer_cast<QueryData>(pair.second);
        query->setCallbackData(pair.second);
        int ref = LuaQuery::createResultTableReference(LUA, *query, *queryData);
        LUA->ReferencePush(ref);
        LuaReferenceFree(LUA, ref);
        LUA->SetTable(-3);
    }
    if (!LuaIQuery::pushCallbackReference(LUA, refs->successReference, refs->tableReference,
                                          "onSuccess", data->isFirstData())) {
        LUA->Pop(); //Table of results
        return;
    }
    LUA->ReferencePush(refs->tableReference);
    LUA->Push(-3); //Table of results
    LuaObject::pcallWithErrorReporter(LUA, 2);

    LUA->Pop(); //Table of results

}

void LuaTransaction::clearAddedQueries(ILuaBase *LUA) {
    for (auto &entry: m_addedQueries) {
        if (entry.tableReference != 0) {
            LuaReferenceFree(LUA, entry.tableReference);
            entry.tableReference = 0;
        }
    }
    m_addedQueries.clear();
}

void LuaTransaction::onDestroyedByLua(ILuaBase *LUA) {
    clearAddedQueries(LUA);
    LuaIQuery::onDestroyedByLua(LUA);
}
