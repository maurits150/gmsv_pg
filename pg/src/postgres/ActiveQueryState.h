// Active PostgreSQL query cancellation state for gmsv_pg.
#ifndef PG_ACTIVEQUERYSTATE_H
#define PG_ACTIVEQUERYSTATE_H

#include <memory>
#include <mutex>

#include <pqxx/pqxx>

#include "IQuery.h"

// Publishes the worker's active libpqxx connection only while PostgreSQL execution is in progress.
class ActiveQueryState {
public:
    class Guard {
    public:
        Guard(ActiveQueryState &state, std::shared_ptr<IQueryData> data, pqxx::connection *connection);
        ~Guard();

        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;

    private:
        ActiveQueryState &state;
    };

    std::shared_ptr<IQueryData> currentData();
    bool cancel(const std::shared_ptr<IQueryData> &data);

private:
    friend class Guard;

    std::mutex mutex;
    std::shared_ptr<IQueryData> activeData;
    pqxx::connection *activeConnection = nullptr;
};

#endif
