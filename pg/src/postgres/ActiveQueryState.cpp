// Active PostgreSQL query cancellation state for gmsv_pg.
#include "ActiveQueryState.h"

ActiveQueryState::Guard::Guard(ActiveQueryState &state, std::shared_ptr<IQueryData> data, pqxx::connection *connection)
        : state(state) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.activeData = std::move(data);
    state.activeConnection = connection;
}

ActiveQueryState::Guard::~Guard() {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.activeData.reset();
    state.activeConnection = nullptr;
}

std::shared_ptr<IQueryData> ActiveQueryState::currentData() {
    std::lock_guard<std::mutex> lock(mutex);
    return activeData;
}

bool ActiveQueryState::cancel(const std::shared_ptr<IQueryData> &data) {
    std::lock_guard<std::mutex> lock(mutex);
    if (activeData != data || activeConnection == nullptr) return false;
    try {
        data->setCancellationRequested(true);
        activeConnection->cancel_query();
        return true;
    } catch (const std::exception &) {
        data->setCancellationRequested(false);
        return false;
    }
}
