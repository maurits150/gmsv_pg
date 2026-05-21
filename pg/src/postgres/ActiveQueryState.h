// Active PostgreSQL query cancellation state for gmsv_pg.
#ifndef PG_ACTIVEQUERYSTATE_H
#define PG_ACTIVEQUERYSTATE_H

#include <memory>
#include <mutex>
#include <vector>

#include <libpq-fe.h>

#include "IQuery.h"

// Publishes the worker's active libpq connection only while PostgreSQL execution is in progress.
class ActiveQueryState {
public:
    class Guard {
    public:
        Guard(ActiveQueryState &state, std::shared_ptr<IQueryData> data, PGconn *connection);
        ~Guard();

        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;

    private:
        ActiveQueryState &state;
    };

    std::shared_ptr<IQueryData> currentData();
    bool cancel(const std::shared_ptr<IQueryData> &data);
    void clear();
    void addAlias(const std::shared_ptr<IQueryData> &data);
    void removeAlias(const std::shared_ptr<IQueryData> &data);

private:
    friend class Guard;

    std::mutex mutex;
    std::shared_ptr<IQueryData> activeData;
    std::vector<std::shared_ptr<IQueryData>> activeAliases;
    PGconn *activeConnection = nullptr;
};

#endif
