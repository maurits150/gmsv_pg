# PostgreSQL Native Feature Completion Plan

Status: TODO
Date: 2026-05-17

## Intent

Finish the PostgreSQL-native features that remain after the MySQLOO-shaped runtime refactor. These features should strengthen PostgreSQL behavior instead of pretending PostgreSQL is MySQL.

The current module has working MySQLOO-shaped database/query/prepared-query/transaction behavior, Docker-backed GMod integration tests, reconnect tests, concurrency tests, and lifecycle tests. This plan covers the remaining intentionally incomplete areas.

## Ownership

- PostgreSQL backend owns SQL execution semantics, connection options, result metadata, notifications, COPY, savepoints, notices, cancellation, and reconnect behavior.
- Lua binding owns public API shape, callback names, object lifetimes, and Lua-thread callback delivery.
- Tests own executable GMod bridge/Docker verification snippets in `TESTS.md`.
- Build/runtime packaging owns Linux runtime dependency deployment and Windows build verification.

## Current Known Gaps

- `COPY FROM` / `COPY TO` is not implemented.
- `LISTEN` / `NOTIFY` receive-side support is not implemented.
- Savepoint helpers are not implemented.
- PostgreSQL notice/warning callbacks are not implemented.
- libpq connection option validation is permissive and incomplete.
- Windows build has not been verified.
- Runtime dependency packaging is improved but still environment-sensitive.

## 1. `query:commandStatus()`

Status: DONE for normal query result chains through direct libpq command tags.

### Direction

Expose PostgreSQL command status for completed queries without weakening the current result model.

Examples of command status strings:

```text
SELECT 3
INSERT 0 1
UPDATE 12
DELETE 4
CREATE TABLE
```

### Implementation

- Direct libpq execution owns each `PGresult` long enough to capture:
  - `PQcmdStatus`
  - `PQcmdTuples`
  - `PQoidValue`
  - column metadata
  - row values
- Convert `PGresult` into the existing `ResultData` / `QueryData` representation immediately, then clear the `PGresult`.
- Keep `query:affectedRows()` backed by command tuples where possible.
- Keep `query:oid()` backed by `PQoidValue`.
- `query:commandStatus()` returns the stored command status string for the current result.

### Tests

Add to `TESTS.md`:

- `SELECT 1` returns status starting with `SELECT`.
- `INSERT ... RETURNING` returns insert status and rows.
- `UPDATE` without `RETURNING` reports affected rows and `UPDATE n`.
- `CREATE TABLE` returns a non-empty command status.

## 2. `COPY FROM` / `COPY TO`

### Direction

Expose PostgreSQL bulk import/export as native PostgreSQL APIs, not as MySQLOO query compatibility methods.

### Public API

Initial text-mode API:

```lua
local copy = db:copyFrom("COPY table_name (a, b) FROM STDIN WITH (FORMAT csv)")
copy:write("1,hello\n")
copy:write("2,world\n")
copy:finish()

function copy:onSuccess(rowsCopied) end
function copy:onError(err) end
```

```lua
local copy = db:copyTo("COPY table_name TO STDOUT WITH (FORMAT csv)")
function copy:onData(chunk) end
function copy:onSuccess() end
function copy:onError(err) end
copy:start()
```

### Implementation

- Add `PgCopyFrom` and `PgCopyTo` operation types or a shared `PgCopyOperation` base.
- COPY operations are queued on a `Database` like queries, but have separate Lua wrappers and callbacks.
- Use libpq COPY APIs directly:
  - `PQputCopyData`
  - `PQputCopyEnd`
  - `PQgetCopyData`
  - `PQresultStatus`
- Start with text/CSV mode.
- Add binary COPY only after text COPY is stable.
- Do not call Lua from the worker thread. Queue data chunks/results for Lua-thread delivery.
- Define memory limits for queued COPY TO chunks so a large export cannot grow unbounded in memory.

### Tests

Add to `TESTS.md`:

- COPY FROM CSV inserts known rows.
- COPY TO CSV returns those rows.
- COPY FROM bad data calls `onError` and rolls back/ends COPY cleanly.
- Large COPY FROM test with at least 10k rows.

## 3. `LISTEN` / `NOTIFY`

### Direction

Use normal queries for `NOTIFY`, but use a dedicated listener runtime path for receiving notifications. Notifications are asynchronous messages, not query result rows.

### Public API

```lua
local listener = db:listen("channel_name")

function listener:onNotify(channel, payload, pid) end
function listener:onError(err) end
function listener:onReconnect() end

listener:start()
listener:unlisten()
```

Convenience notify API:

```lua
db:notify("channel_name", "payload")
```

### Implementation

- `db:notify(channel, payload)` can use a normal parameterized query:
  - `SELECT pg_notify($1, $2)`
- Receiving notifications should use a dedicated PostgreSQL connection owned by a listener object.
- Do not block the normal database query worker waiting for notifications.
- Listener worker should:
  - connect using the same connection options as the parent DB
  - issue `LISTEN` commands for subscribed channels
  - wait/poll for socket readability
  - call `PQconsumeInput` and `PQnotifies`
  - queue notification callbacks to Lua thread
- Channel names must be quoted as identifiers when issuing `LISTEN` / `UNLISTEN`.
- Payload is text.
- Reconnect should resubscribe only channels owned by that listener object.

### Tests

Add to `TESTS.md`:

- listener receives `db:notify` payload.
- multiple channels route correctly.
- unlisten stops callbacks.
- listener reconnect after Docker restart resubscribes and receives later notifications.
- payload with quotes/unicode survives.

## 4. Savepoints

### Direction

Expose savepoints as PostgreSQL-native transaction helpers. Do not change the MySQLOO-compatible staged transaction semantics.

### Public API

For staged transactions, add operation objects:

```lua
tx:savepoint("name")
tx:rollbackTo("name")
tx:releaseSavepoint("name")
```

These stage savepoint commands alongside normal transaction queries.

### Implementation

- Add transaction child operation type for savepoint commands.
- Validate savepoint names are non-empty strings.
- Quote savepoint identifiers safely with PostgreSQL identifier quoting.
- Execute staged operations in order inside the transaction worker.
- Rollback behavior:
  - `rollbackTo(name)` should roll back to that savepoint and continue executing later staged operations.
  - unhandled SQL errors still roll back the whole transaction.

### Tests

Add to `TESTS.md`:

- insert A, savepoint, insert B, rollbackTo, insert C, commit -> A and C persist, B does not.
- releaseSavepoint works.
- invalid savepoint name throws.
- rollbackTo missing savepoint triggers transaction error and full rollback.

## 5. Notice / Warning Callbacks

### Direction

Expose PostgreSQL notices separately from query errors.

### Public API

Database-level callbacks:

```lua
function db:onNotice(message, severity, sqlstate) end
function db:onWarning(message, severity, sqlstate) end
```

If severity separation is unreliable in the current libpq path, expose `onNotice` first and include severity text when available.

### Implementation

- Use libpq notice receiver hooks on the underlying `PGconn`.
- Queue notices to Lua thread; never call Lua from libpq/backend worker context.
- Notices should not fail the query unless PostgreSQL reports an actual error.

### Tests

Add to `TESTS.md`:

- `DO $$ BEGIN RAISE NOTICE 'hello'; END $$;` calls notice callback.
- warning severity routes to warning or notice callback.
- normal query success still fires after notice.

## 6. libpq Connection Option Validation

### Direction

Keep table-based connection options PostgreSQL-native and broad, but validate/escape them correctly.

### Implementation

- Use libpq option parsing where possible:
  - `PQconninfoParse`
  - `PQconndefaults`
- Accept option tables with libpq option names.
- Reject keys that cannot be represented safely.
- Ensure values are connection-string escaped correctly.
- Preserve options needed for reconnect/listener connections.
- Add tests for SSL options, `application_name`, `connect_timeout`, `hostaddr`, and invalid option names.

### Tests

Add to `TESTS.md`:

- option table sets `application_name`, verified through `current_setting('application_name')`.
- invalid option name throws before connect if validation supports it.
- DSN and option table produce equivalent connection behavior.

## 7. Windows Build Verification

### Direction

Verify that the new source layout and MySQLOO-derived runtime still build the Windows module.

### Implementation

- Generate project files on a Windows-capable environment.
- Confirm vendored headers and LGPL notices are included.
- Confirm linking with Windows libpq artifacts.
- Confirm runtime dependencies in `runtime_depends/windows` still match the built binary.

### Tests

- Build `gmsv_pg_win32.dll`.
- Load in Garry's Mod Windows server/client.
- Run at least module load, connect, raw query, prepared query, transaction, and reconnect smoke tests.

## 8. Runtime Dependency Packaging

### Direction

Keep dynamic libpq packaging unless a reliable static libpq build is introduced.

### Implementation

- Continue bundling `runtime_depends/linux/libpq.so.5`.
- Document how to refresh it from `libpq-dev:i386` when OpenSSL/runtime libraries differ.
- Avoid hardcoded system paths in build scripts.
- Consider a script:

```sh
scripts/refresh-linux-runtime-deps.sh
```

that copies the host i386 `libpq.so.5` into `runtime_depends/linux/` and prints its `ldd` dependency summary.

### Tests

- `ldd pg/bin/gmsv_pg_linux.dll` resolves `libpq.so.5` when copied to GMod root/bin.
- GMod `require('pg')` succeeds after relaunch.
- Documented stale OpenSSL case has remediation steps.

## Suggested Order

1. libpq connection option validation.
2. LISTEN/NOTIFY dedicated listener connection.
3. COPY text mode.
4. Savepoints.
5. Notice callbacks.
6. Windows verification.
7. Runtime dependency refresh script.

## Done Criteria

- Each feature has executable tests in `TESTS.md`.
- Tests pass through the GMod Lua bridge against Docker PostgreSQL.
- No PostgreSQL-native behavior is hidden behind MySQL compatibility shims.
- No Lua callbacks are called from backend worker threads.
- No hardcoded local system paths are added to build scripts.
