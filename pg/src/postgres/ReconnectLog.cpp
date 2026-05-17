// Reconnect event log for gmsv_pg.
#include "ReconnectLog.h"

void ReconnectLog::add(bool success, const std::string &error) {
    std::lock_guard<std::mutex> lock(mutex);
    events.emplace_back(success, success ? "" : error);
}

std::deque<std::pair<bool, std::string>> ReconnectLog::take() {
    std::lock_guard<std::mutex> lock(mutex);
    auto out = events;
    events.clear();
    return out;
}
