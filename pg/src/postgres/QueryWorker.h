// Query worker queue state for gmsv_pg.
#ifndef PG_QUERYWORKER_H
#define PG_QUERYWORKER_H

#include <deque>
#include <memory>
#include <utility>

#include "../BlockingQueue.h"
#include "IQuery.h"

// Owns queued and finished query execution lists for a database worker.
class QueryWorker {
public:
    using QueryPair = std::pair<std::shared_ptr<IQuery>, std::shared_ptr<IQueryData>>;

    void enqueue(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    bool swapToFront(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    bool removeQueued(const std::shared_ptr<IQuery> &query, const std::shared_ptr<IQueryData> &data);
    QueryAbortResult abortQueued();
    QueryAbortResult abortQueuedAndClose();
    void completeQueuedWithError(const std::string &reason);
    void completeQueuedWithErrorAndClose(const std::string &reason);

    bool takeNext(QueryPair &out);
    void finish(const QueryPair &pair);
    std::deque<QueryPair> takeFinished();
    size_t queueSize() const;
    void close();
    bool isClosed() const;

private:
    BlockingQueue<QueryPair> finishedQueries;
    BlockingQueue<QueryPair> queryQueue;
};

#endif
