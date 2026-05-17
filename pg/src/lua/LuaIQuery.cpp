// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.

#include "LuaIQuery.h"
#include "LuaQuery.h"
#include "LuaTransaction.h"
#include "LuaDatabase.h"

PG_LUA_FUNCTION(start) {
    auto query = LuaIQuery::getLuaObject<LuaIQuery>(LUA);
    auto queryData = query->buildQueryData(LUA, 1, true);
    try {
        query->m_query->start(queryData);
    } catch (...) {
        LuaIQuery::finishLuaQueryData(LUA, query->m_query, queryData);
        throw;
    }
    return 0;
}

PG_LUA_FUNCTION(error) {
    auto query = LuaIQuery::getLuaObject<LuaIQuery>(LUA);
    LUA->PushString(query->m_query->error().c_str());
    return 1;
}

PG_LUA_FUNCTION(wait) {
    bool shouldSwap = false;
    if (LUA->IsType(2, GarrysMod::Lua::Type::Bool)) {
        shouldSwap = LUA->GetBool(2);
    }
    auto query = LuaIQuery::getLuaObject<LuaIQuery>(LUA);
    query->m_query->wait(shouldSwap);
    if (query->m_databaseReference != 0) {
        LUA->ReferencePush(query->m_databaseReference);
        auto database = LuaObject::getLuaObject<LuaDatabase>(LUA, -1);
        database->think(LUA);
        LUA->Pop();
    }

    return 0;
}

PG_LUA_FUNCTION(setOption) {
    auto query = LuaIQuery::getLuaObject<LuaIQuery>(LUA);
    LUA->CheckType(2, GarrysMod::Lua::Type::Number);
    bool set = true;
    int option = (int) LUA->GetNumber(2);

    if (LUA->Top() >= 3) {
        LUA->CheckType(3, GarrysMod::Lua::Type::Bool);
        set = LUA->GetBool(3);
    }
    query->m_query->setOption(option, set);
    return 0;
}

PG_LUA_FUNCTION(isRunning) {
    auto query = LuaIQuery::getLuaObject<LuaIQuery>(LUA);
    LUA->PushBool(query->m_query->isRunning());
    return 1;
}

PG_LUA_FUNCTION(abort) {
    auto query = LuaIQuery::getLuaObject<LuaIQuery>(LUA);
    auto abortResult = query->m_query->abort();
    for (auto &pair: abortResult.completed) {
        auto &data = pair.second;
        if (auto transaction = std::dynamic_pointer_cast<Transaction>(pair.first)) {
            LuaTransaction::runAbortedCallback(LUA, transaction, std::dynamic_pointer_cast<TransactionData>(data));
        } else {
            LuaIQuery::runAbortedCallback(LUA, data);
        }
        LuaIQuery::finishLuaQueryData(LUA, pair.first, data);
    }
    LUA->PushBool(abortResult.requested);
    return 1;
}

void LuaIQuery::runAbortedCallback(ILuaBase *LUA, const std::shared_ptr<IQueryData> &data) {
    auto refs = getCallbackReferences(data);
    if (!refs || refs->tableReference == 0) return;

    if (!LuaIQuery::pushCallbackReference(LUA, refs->abortReference, refs->tableReference,
                                          "onAborted", data->isFirstData())) {
        return;
    }
    LUA->ReferencePush(refs->tableReference);
    LuaObject::pcallWithErrorReporter(LUA, 1);
}

void LuaIQuery::runErrorCallback(ILuaBase *LUA, const std::shared_ptr<IQuery> &iQuery,
                                 const std::shared_ptr<IQueryData> &data) {
    auto refs = getCallbackReferences(data);
    if (!refs || refs->tableReference == 0) return;

    if (!LuaIQuery::pushCallbackReference(LUA, refs->errorReference, refs->tableReference,
                                          "onError", data->isFirstData())) {
        return;
    }
    LUA->ReferencePush(refs->tableReference);
    auto error = data->getError();
    LUA->PushString(error.c_str());
    LUA->PushString(iQuery->getSQLString().c_str());
    LuaObject::pcallWithErrorReporter(LUA, 3);
}

void LuaIQuery::addMetaTableFunctions(ILuaBase *LUA) {
    LuaObject::addMetaTableFunctions(LUA);

    LUA->PushCFunction(start);
    LUA->SetField(-2, "start");
    LUA->PushCFunction(error);
    LUA->SetField(-2, "error");
    LUA->PushCFunction(wait);
    LUA->SetField(-2, "wait");
    LUA->PushCFunction(setOption);
    LUA->SetField(-2, "setOption");
    LUA->PushCFunction(isRunning);
    LUA->SetField(-2, "isRunning");
    LUA->PushCFunction(abort);
    LUA->SetField(-2, "abort");
}

void LuaIQuery::referenceCallbacks(ILuaBase *LUA, int stackPosition, IQueryData &data) {
    auto refs = dynamic_cast<LuaCallbackReferences *>(&data);
    if (refs == nullptr) throw PGException("pg: Lua callback references require Lua-owned query data");
    LUA->Push(stackPosition);
    refs->tableReference = LuaReferenceCreate(LUA);

    if (refs->successReference == 0) {
        refs->successReference = getFunctionReference(LUA, stackPosition, "onSuccess");
    }

    if (refs->abortReference == 0) {
        refs->abortReference = getFunctionReference(LUA, stackPosition, "onAborted");
    }

    if (refs->onDataReference == 0) {
        refs->onDataReference = getFunctionReference(LUA, stackPosition, "onData");
    }

    if (refs->errorReference == 0) {
        refs->errorReference = getFunctionReference(LUA, stackPosition, "onError");
    }
}

LuaCallbackReferences *LuaIQuery::getCallbackReferences(const std::shared_ptr<IQueryData> &data) {
    return dynamic_cast<LuaCallbackReferences *>(data.get());
}

void LuaIQuery::finishLuaQueryData(ILuaBase *LUA, const std::shared_ptr<IQuery> &query,
                                   const std::shared_ptr<IQueryData> &data) {
    if (auto transactionData = std::dynamic_pointer_cast<TransactionData>(data)) {
        for (auto &pair : transactionData->m_queries) {
            pair.first->finishQueryData(pair.second);
        }
    }

    if (auto refs = getCallbackReferences(data)) {
        if (refs->tableReference != 0) LuaReferenceFree(LUA, refs->tableReference);
        if (refs->successReference != 0) LuaReferenceFree(LUA, refs->successReference);
        if (refs->errorReference != 0) LuaReferenceFree(LUA, refs->errorReference);
        if (refs->abortReference != 0) LuaReferenceFree(LUA, refs->abortReference);
        if (refs->onDataReference != 0) LuaReferenceFree(LUA, refs->onDataReference);
        refs->tableReference = 0;
        refs->successReference = 0;
        refs->errorReference = 0;
        refs->abortReference = 0;
        refs->onDataReference = 0;
    }

    query->finishQueryData(data);
}

void
LuaIQuery::runCallback(ILuaBase *LUA, const std::shared_ptr<IQuery> &iQuery, const std::shared_ptr<IQueryData> &data) {
    iQuery->setCallbackData(data);

    auto status = data->getResultStatus();
    if (data->getStatus() == QUERY_ABORTED) {
        if (auto transaction = std::dynamic_pointer_cast<Transaction>(iQuery)) {
            LuaTransaction::runAbortedCallback(LUA, transaction, std::dynamic_pointer_cast<TransactionData>(data));
        } else {
            LuaIQuery::runAbortedCallback(LUA, data);
        }
    } else switch (status) {
        case QUERY_NONE:
            break; //Should not happen
        case QUERY_ERROR:
            if (auto transaction = std::dynamic_pointer_cast<Transaction>(iQuery)) {
                LuaTransaction::runErrorCallback(LUA, transaction, std::dynamic_pointer_cast<TransactionData>(data));
            } else {
                LuaIQuery::runErrorCallback(LUA, iQuery, data);
            }
            break;
        case QUERY_SUCCESS:
            if (auto query = std::dynamic_pointer_cast<Query>(iQuery)) {
                LuaQuery::runSuccessCallback(LUA, query, std::dynamic_pointer_cast<QueryData>(data));
            } else if (auto transaction = std::dynamic_pointer_cast<Transaction>(iQuery)) {
                LuaTransaction::runSuccessCallback(LUA, transaction, std::dynamic_pointer_cast<TransactionData>(data));
            }
            break;
    }

    LuaIQuery::finishLuaQueryData(LUA, iQuery, data);
}

void LuaIQuery::onDestroyedByLua(ILuaBase *LUA) {
    if (m_databaseReference != 0) {
        LuaReferenceFree(LUA, m_databaseReference);
        m_databaseReference = 0;
    }
}
