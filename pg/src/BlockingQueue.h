// Derived from MySQLOO runtime code (LGPL-2.1); adapted for PostgreSQL gmsv_pg.
#ifndef BLOCKING_QUEUE_
#define BLOCKING_QUEUE_

#include <deque>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <functional>
#include <utility>

template<typename T>
class BlockingQueue {
public:
    bool put(T elem) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed) return false;
            backingQueue.push_back(std::move(elem));
        }
        waitObj.notify_all();
        return true;
    }

    bool empty() {
        return size() == 0;
    }

    bool swapToFrontIf(std::function<bool(T)> func) {
        std::lock_guard<std::mutex> lock(mutex);
        auto pos = std::find_if(backingQueue.begin(), backingQueue.end(), func);
        if (pos != backingQueue.begin() && pos != backingQueue.end()) {
            std::iter_swap(pos, backingQueue.begin());
            return true;
        }
        return false;
    }

    bool removeIf(std::function<bool(T)> func) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::remove_if(backingQueue.begin(), backingQueue.end(), func);
        bool removed = it != backingQueue.end();
        backingQueue.erase(it, backingQueue.end());
        return removed;
    }

    void remove(T elem) {
        std::lock_guard<std::mutex> lock(mutex);
        backingQueue.erase(std::remove(backingQueue.begin(), backingQueue.end(), elem), backingQueue.end());
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex);
        return backingQueue.size();
    }

    bool take(T &out) {
        std::unique_lock<std::mutex> lock(mutex);
        waitObj.wait(lock, [this] { return closed || !backingQueue.empty(); });
        if (backingQueue.empty()) return false;
        out = std::move(backingQueue.front());
        backingQueue.pop_front();
        return true;
    }

    std::deque<T> clear() {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<T> returnQueue = std::move(backingQueue);
        backingQueue.clear();
        return returnQueue;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            closed = true;
        }
        waitObj.notify_all();
    }

    bool isClosed() {
        std::lock_guard<std::mutex> lock(mutex);
        return closed;
    }

private:
    std::deque<T> backingQueue{};
    bool closed = false;
    std::mutex mutex{};
    std::condition_variable waitObj{};
};

#endif
