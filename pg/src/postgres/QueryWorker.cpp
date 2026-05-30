// Query worker queue state for gmsv_pg.
#include "QueryWorker.h"

#include "PGException.h"

void QueryWorker::enqueue(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    data->setStatus(QUERY_WAITING);
    if (!queryQueue.put(std::make_pair(query, data))) {
        data->setStatus(QUERY_ABORTED);
        data->setFinished(true);
        throw PGException("Database is disconnected.");
    }
}

bool QueryWorker::swapToFront(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    return queryQueue.swapToFrontIf([&](const QueryPair &pair) {
        return pair.first == query && pair.second == data;
    });
}

bool QueryWorker::removeQueued(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data) {
    return queryQueue.removeIf([&](const QueryPair &pair) {
        return pair.first == query && pair.second == data;
    });
}

bool QueryWorker::completeQueuedWithError(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data,
                                          const std::string &reason) {
    bool removed = removeQueued(query, data);
    if (!removed) return false;

    data->setError(reason);
    data->setResultStatus(QUERY_ERROR);
    data->setStatus(QUERY_COMPLETE);
    finishedQueries.put(std::make_pair(query, data));
    data->setFinished(true);
    return true;
}

QueryAbortResult QueryWorker::abortQueued() {
    QueryAbortResult result;
    auto canceled = queryQueue.clear();
    for (auto &pair : canceled) {
        if (!pair.second) continue;
        pair.second->setStatus(QUERY_ABORTED);
        pair.second->setFinished(true);
        result.completed.push_back(pair);
        result.requested = true;
        result.requestedCount++;
    }
    return result;
}

QueryAbortResult QueryWorker::abortQueuedAndClose() {
    QueryAbortResult result;
    auto canceled = queryQueue.closeAndClear();
    for (auto &pair : canceled) {
        if (!pair.second) continue;
        pair.second->setStatus(QUERY_ABORTED);
        pair.second->setFinished(true);
        result.completed.push_back(pair);
        result.requested = true;
        result.requestedCount++;
    }
    return result;
}

void QueryWorker::completeQueuedWithError(const std::string &reason) {
    auto queuedQueries = queryQueue.clear();
    for (auto &pair : queuedQueries) {
        if (!pair.second) continue;
        pair.second->setError(reason);
        pair.second->setResultStatus(QUERY_ERROR);
        pair.second->setStatus(QUERY_COMPLETE);
        finishedQueries.put(pair);
        pair.second->setFinished(true);
    }
}

void QueryWorker::completeQueuedWithErrorAndClose(const std::string &reason) {
    auto queuedQueries = queryQueue.closeAndClear();
    for (auto &pair : queuedQueries) {
        if (!pair.second) continue;
        pair.second->setError(reason);
        pair.second->setResultStatus(QUERY_ERROR);
        pair.second->setStatus(QUERY_COMPLETE);
        finishedQueries.put(pair);
        pair.second->setFinished(true);
    }
}

bool QueryWorker::takeNext(QueryPair &out) { return queryQueue.take(out); }

void QueryWorker::finish(const QueryPair &pair) { finishedQueries.put(pair); }

std::deque<QueryWorker::QueryPair> QueryWorker::takeFinished() { return finishedQueries.clear(); }

size_t QueryWorker::queueSize() const { return queryQueue.size(); }

void QueryWorker::close() { queryQueue.close(); }

bool QueryWorker::isClosed() const { return queryQueue.isClosed(); }
