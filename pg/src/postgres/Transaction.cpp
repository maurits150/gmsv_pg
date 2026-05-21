// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "Transaction.h"

#include "Database.h"
#include "LibpqExecutor.h"

std::shared_ptr<Transaction> Transaction::create(const std::shared_ptr<Database> &database) {
    return std::shared_ptr<Transaction>(new Transaction(database));
}

void Transaction::validateStart(const std::shared_ptr<IQueryData> &) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (hasBeenStarted) {
        throw PGException("Transaction already started.");
    }
}

void Transaction::executeStatement(Database &database, PGconn *connection,
                                    const std::shared_ptr<IQueryData> &ptr) {
    auto data = std::dynamic_pointer_cast<TransactionData>(ptr);
    try {
        LibpqExecutor::executeCommand(connection, "BEGIN");
        try {
            executeInTransaction(database, connection, ptr);
        } catch (...) {
            try { LibpqExecutor::executeCommand(connection, "ROLLBACK"); } catch (...) {}
            throw;
        }

        try {
            LibpqExecutor::executeCommand(connection, "COMMIT");
        } catch (const PGConnectionException &error) {
            std::string message = std::string("Transaction commit outcome unknown: ") + error.what();
            for (auto &pair : data->m_queries) {
                pair.second->setResultStatus(QUERY_ERROR);
                pair.second->setStatus(QUERY_COMPLETE);
                pair.second->setError(message);
                pair.second->setFinished(true);
            }
            data->setResultStatus(QUERY_ERROR);
            data->setError(message);
            throw;
        }

        for (auto &pair : data->m_queries) {
            pair.second->setResultStatus(QUERY_SUCCESS);
            pair.second->setStatus(QUERY_COMPLETE);
            pair.second->setFinished(true);
        }
        data->setResultStatus(QUERY_SUCCESS);
    } catch (const std::exception &error) {
        for (auto &pair : data->m_queries) {
            if (pair.second->isCancellationRequested()) data->setCancellationRequested(true);
        }
        bool commitOutcomeUnknown = data->getError().find("Transaction commit outcome unknown") != std::string::npos;
        for (auto &pair : data->m_queries) {
            if (data->isCancellationRequested()) {
                pair.second->setResultStatus(QUERY_NONE);
                pair.second->setStatus(QUERY_ABORTED);
            } else if (pair.second->getResultStatus() != QUERY_ERROR) {
                pair.second->setResultStatus(QUERY_ERROR);
                pair.second->setStatus(QUERY_COMPLETE);
                pair.second->setError(std::string("Transaction rolled back: ") + error.what());
            }
            pair.second->setFinished(true);
        }
        if (!commitOutcomeUnknown) {
            data->setResultStatus(QUERY_ERROR);
            data->setError(std::string("Transaction rolled back: ") + error.what());
        }
        throw;
    }
}

void Transaction::executeInTransaction(Database &database, PGconn *connection,
                                        const std::shared_ptr<IQueryData> &ptr) {
    auto data = std::dynamic_pointer_cast<TransactionData>(ptr);
    for (auto &pair : data->m_queries) {
        pair.second->setStatus(QUERY_RUNNING);
        database.addActiveQueryAlias(pair.second);
        try {
            pair.first->executeInTransaction(database, connection, pair.second);
            if (PQtransactionStatus(connection) != PQTRANS_INTRANS) {
                data->setError("Transaction outcome unknown: child query ended the surrounding transaction");
                throw PGException("pg: transaction child query ended the surrounding transaction");
            }
        } catch (...) {
            database.removeActiveQueryAlias(pair.second);
            throw;
        }
        database.removeActiveQueryAlias(pair.second);
    }
}
