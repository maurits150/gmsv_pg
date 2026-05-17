// Reconnect event log for gmsv_pg.
#ifndef PG_RECONNECTLOG_H
#define PG_RECONNECTLOG_H

#include <deque>
#include <mutex>
#include <string>
#include <utility>

class ReconnectLog {
public:
    void add(bool success, const std::string &error);
    std::deque<std::pair<bool, std::string>> take();

private:
    std::mutex mutex;
    std::deque<std::pair<bool, std::string>> events;
};

#endif
