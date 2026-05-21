// Active PostgreSQL query cancellation state for gmsv_pg.
#include "ActiveQueryState.h"

#include <algorithm>

ActiveQueryState::Guard::Guard(ActiveQueryState &state, std::shared_ptr<IQueryData> data, PGconn *connection)
        : state(state) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.activeData = std::move(data);
    state.activeConnection = connection;
}

ActiveQueryState::Guard::~Guard() {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.activeData.reset();
    state.activeAliases.clear();
    state.activeConnection = nullptr;
}

std::shared_ptr<IQueryData> ActiveQueryState::currentData() {
    std::lock_guard<std::mutex> lock(mutex);
    return activeData;
}

void ActiveQueryState::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    activeData.reset();
    activeAliases.clear();
    activeConnection = nullptr;
}

void ActiveQueryState::addAlias(const std::shared_ptr<IQueryData> &data) {
    std::lock_guard<std::mutex> lock(mutex);
    if (activeConnection != nullptr) activeAliases.push_back(data);
}

void ActiveQueryState::removeAlias(const std::shared_ptr<IQueryData> &data) {
    std::lock_guard<std::mutex> lock(mutex);
    activeAliases.erase(std::remove(activeAliases.begin(), activeAliases.end(), data), activeAliases.end());
}

bool ActiveQueryState::cancel(const std::shared_ptr<IQueryData> &data) {
    PGcancel *cancel = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool matchesActive = activeData == data ||
                             std::find(activeAliases.begin(), activeAliases.end(), data) != activeAliases.end();
        if (!matchesActive || activeConnection == nullptr) return false;
        data->setCancellationRequested(true);
        cancel = PQgetCancel(activeConnection);
        if (!cancel) {
            data->setCancellationRequested(false);
            return false;
        }
    }

    char errorBuffer[256] = {0};
    int success = PQcancel(cancel, errorBuffer, sizeof(errorBuffer));
    PQfreeCancel(cancel);
    if (success == 1) {
        return true;
    }
    data->setCancellationRequested(false);
    return false;
}
