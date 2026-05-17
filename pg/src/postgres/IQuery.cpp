// PostgreSQL backend for gmsv_pg; integrates with the MySQLOO-derived Lua runtime.
#include "IQuery.h"

#include <algorithm>

#include "Database.h"

IQuery::IQuery(std::shared_ptr<Database> database) : m_database(std::move(database)) {}

IQuery::~IQuery() = default;

void IQuery::start(const std::shared_ptr<IQueryData> &queryData) {
    addQueryData(queryData);
    m_database->enqueueQuery(shared_from_this(), queryData);
    hasBeenStarted = true;
}

bool IQuery::isRunning() {
    for (const auto &data : runningQueryData) {
        QueryStatus status = data->getStatus();
        if (status == QUERY_RUNNING || status == QUERY_WAITING) return true;
    }
    return false;
}

void IQuery::setOption(int option, bool enabled) {
    if (option != OPTION_NUMERIC_FIELDS && option != OPTION_NAMED_FIELDS &&
        option != OPTION_INTERPRET_DATA && option != OPTION_CACHE) {
        throw PGException("Invalid query option");
    }
    if (enabled) m_options |= option;
    else m_options &= ~option;
}

void IQuery::addQueryData(const std::shared_ptr<IQueryData> &data) {
    if (!hasBeenStarted) {
        data->m_wasFirstData = true;
    }
    runningQueryData.push_back(data);
}

void IQuery::finishQueryData(const std::shared_ptr<IQueryData> &data) {
    if (runningQueryData.empty()) return;
    if (runningQueryData.front() == data) {
        runningQueryData.pop_front();
        return;
    }
    runningQueryData.erase(std::remove(runningQueryData.begin(), runningQueryData.end(), data), runningQueryData.end());
}

std::string IQuery::error() const {
    return callbackQueryData ? callbackQueryData->getError() : "";
}

QueryAbortResult IQuery::abort() {
    QueryAbortResult result;
    auto database = m_database;
    if (!database) return result;

    auto runningQueries = runningQueryData;
    for (auto &data : runningQueries) {
        auto singleResult = database->abortQuery(shared_from_this(), data);
        if (singleResult.requested) result.requested = true;
        result.requestedCount += singleResult.requestedCount;
        result.completed.insert(result.completed.end(), singleResult.completed.begin(), singleResult.completed.end());
    }
    return result;
}

void IQuery::wait(bool shouldSwap) {
    if (runningQueryData.empty()) throw PGException("Query not started");
    auto data = runningQueryData.back();
    if (shouldSwap) m_database->swapQueryToFront(shared_from_this(), data);
    m_database->waitForQuery(shared_from_this(), data);
}

QueryResultStatus IQuery::getResultStatus() const {
    return callbackQueryData ? callbackQueryData->getResultStatus() : QUERY_NONE;
}

bool IQueryData::isFinished() { return finished; }
void IQueryData::setFinished(bool isFinished) {
    finished = isFinished;
    m_finishCondition.notify_all();
}

void IQueryData::waitUntilFinished() {
    std::unique_lock<std::mutex> lock(m_finishMutex);
    m_finishCondition.wait(lock, [this] { return finished.load(); });
}
void IQueryData::setCancellationRequested(bool requested) { cancellationRequested = requested; }
bool IQueryData::isCancellationRequested() const { return cancellationRequested; }
QueryStatus IQueryData::getStatus() { return m_status; }
void IQueryData::setStatus(QueryStatus status) { m_status = status; }
QueryResultStatus IQueryData::getResultStatus() { return m_resultStatus; }
void IQueryData::setResultStatus(QueryResultStatus status) { m_resultStatus = status; }
