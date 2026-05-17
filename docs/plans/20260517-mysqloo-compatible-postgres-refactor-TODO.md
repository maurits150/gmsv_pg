# MySQLOO-Compatible PostgreSQL Refactor Plan

Status: TODO
Date: 2026-05-17

## Goal

Refactor `gmsv_pg` into a PostgreSQL-first Garry's Mod module whose Lua API is familiar enough for MySQLOO users to migrate with minimal application-code churn.

Implementation direction: use MySQLOO's proven runtime/API architecture as the base and replace its MySQL backend with a PostgreSQL backend. This is more viable than growing the current small `gmsv_pg` implementation into a MySQLOO-compatible runtime because MySQLOO already has the desired Lua object model, async queue, Think-hook callback delivery, query lifecycle, transaction staging, wait/abort behavior, and reconnect flow.

Because this means copying or adapting MySQLOO implementation code, the derived portions must remain LGPL-2.1. This is acceptable for this project, but the licensing boundary must be explicit in source files and documentation.

This is not a plan to disguise PostgreSQL as MySQL. The module should expose PostgreSQL behavior honestly, while matching MySQLOO's object model, lifecycle methods, callback names, and result-table shape where those concepts transfer cleanly.

The intended migration path is:

1. Port SQL from MySQL dialect to PostgreSQL dialect.
2. Verify the application against `pg` using the compatibility API.
3. Mechanically replace MySQLOO module references with `pg` references.

For example, migrated code should look like:

```lua
local pg = require("pg")
local db = pg.connect(...)
```

instead of:

```lua
require("mysqloo")
local db = mysqloo.connect(...)
```

The goal is source-level compatibility after replacing `mysqloo` references with `pg`. Runtime shims such as `mysqloo = pg` may be useful during local testing, but they are not the desired long-term integration pattern.

Compatibility should focus on code structure rather than SQL semantics. Existing control flow, callback setup, query lifecycle code, and result consumption should survive migration when possible. SQL syntax, insert-id behavior, server options, and type semantics should follow PostgreSQL.

Compatibility must not hide PostgreSQL semantics or degrade PostgreSQL-native features. MySQL-only behavior should be implemented only when it maps cleanly, otherwise it should be a documented no-op or explicit unsupported behavior.

## Non-Goals

- Do not translate arbitrary MySQL SQL dialect into PostgreSQL SQL.
- Do not fake `LAST_INSERT_ID()` semantics globally. Prefer PostgreSQL `RETURNING`.
- Do not make PostgreSQL prepared statements worse just to mimic MySQL internals.
- Do not require users to install files into Garry's Mod from the build system. Users can symlink or copy binaries themselves.
- Do not add backward-compatible aliases that conflict with PostgreSQL-native behavior unless the compatibility value is clear.
- Do not preserve the current `gmsv_pg` runtime architecture if it conflicts with the MySQLOO-compatible object/queue/callback model.

## Current State

The current `pg` API exposes a custom connection/query model:

```lua
local pg = require("pg")
local conn = pg.new_connection()
conn:connect(...)
local query = conn:query("SELECT 1")
query:on("success", function(rows) end)
query:run()
```

Important gaps relative to MySQLOO:

- No `pg.connect(...)` constructor matching `mysqloo.connect(...)`.
- No MySQLOO constants.
- No database/query status model.
- No `query:start()`, `query:wait()`, `query:abort()` shape.
- No callback fields like `query.onSuccess = function(...) end`.
- No `query:getData()`, `query:error()`, `affectedRows()`, or `hasMoreResults()` shape.
- Prepared query API does not match MySQLOO setters.
- Transactions are not exposed as first-class objects.
- Query scheduling is global and fragile.
- Prepared query implementation needs review before relying on it.

## Public API Target

### Module Table

Expose these on `pg` so MySQLOO call sites can be migrated by replacing `mysqloo` with `pg`:

```lua
pg.connect(host, username, password, database, port, socketOrOptions)
pg.new_connection(optionsOrConnectionString)
pg.VERSION
pg.MINOR_VERSION
```

Retain a PostgreSQL-native path:

```lua
pg.connect(connectionString)
pg.connect({ host = "...", user = "...", password = "...", dbname = "..." })
```

### Constants

Export MySQLOO-compatible numeric constants with the same values where possible:

```lua
pg.DATABASE_CONNECTED
pg.DATABASE_CONNECTING
pg.DATABASE_NOT_CONNECTED
pg.DATABASE_CONNECTION_FAILED

pg.QUERY_NOT_RUNNING
pg.QUERY_RUNNING
pg.QUERY_COMPLETE
pg.QUERY_ABORTED
pg.QUERY_WAITING

pg.OPTION_NUMERIC_FIELDS
pg.OPTION_NAMED_FIELDS
pg.OPTION_INTERPRET_DATA
pg.OPTION_CACHE
```

PostgreSQL SSL constants should be PostgreSQL-native security settings. The MySQLOO-like names are only migration conveniences for the same security levels:

```lua
pg.SSL_MODE_DISABLED        -- sslmode=disable
pg.SSL_MODE_PREFERRED       -- sslmode=prefer
pg.SSL_MODE_REQUIRED        -- sslmode=require
pg.SSL_MODE_VERIFY_CA       -- sslmode=verify-ca
pg.SSL_MODE_VERIFY_IDENTITY -- sslmode=verify-full
```

### Database Methods

Implement the MySQLOO-shaped database methods:

```lua
db:connect()
db:disconnect(shouldWait)
db:query(sql)
db:prepare(sql)
db:createTransaction()
db:escape(str)
db:abortAllQueries()
db:status()
db:wait()
db:serverVersion()
db:serverInfo()
db:hostInfo()
db:queueSize()
db:ping()
db:setCharacterSet(charset)
db:setReadTimeout(seconds)
db:setWriteTimeout(seconds)
db:setConnectTimeout(seconds)
db:setSSLMode(mode)
db:setSSLSettings(key, cert, ca, capath, cipher)
```

Compatibility methods with limited PostgreSQL meaning:

```lua
db:setAutoReconnect(bool)
db:setMultiStatements(bool)
db:setCachePreparedStatements(bool)
```

Callback fields:

```lua
function db:onConnected() end
function db:onConnectionFailed(err) end
function db:onDisconnected() end
function db:onReconnect() end
function db:onReconnectFailed(err) end
```

`onReconnect` and `onReconnectFailed` are PostgreSQL-native additions. MySQLOO compatibility behavior should not call `onConnected` again after an internal reconnect.

### Query Methods

Implement:

```lua
query:start()
query:isRunning()
query:getData()
query:abort()
query:affectedRows()
query:setOption(option, enabled)
query:wait(shouldSwap)
query:error()
query:hasMoreResults()
query:getNextResults()
```

PostgreSQL-native additions:

```lua
query:commandStatus()
query:oid()
```

Compatibility method with explicit PostgreSQL semantics:

```lua
query:lastInsert()
```

`lastInsert()` must not pretend PostgreSQL has MySQL's connection-scoped insert id. It should fail loudly so migrated code does not silently store `nil` or `0` as an id.

- Throw a Lua error explaining that `lastInsert()` is MySQL-specific.
- Document `INSERT ... RETURNING` as the replacement.

Callback fields:

```lua
function query:onAborted() end
function query:onError(err, sql) end
function query:onSuccess(data) end
function query:onData(row) end
```

The existing event-emitter API can remain as a PostgreSQL-native convenience if it does not complicate the core model:

```lua
query:on("success", fn)
query:on("error", fn)
```

### Prepared Query Methods

Prepared queries inherit query methods and add:

```lua
prepared:setNumber(index, number)
prepared:setString(index, string)
prepared:setBoolean(index, bool)
prepared:setNull(index)
prepared:clearParameters()
prepared:putNewParameters()
```

PostgreSQL-native additions:

```lua
prepared:setJson(index, value)
prepared:setBinary(index, bytes)
prepared:setArray(index, values)
```

Placeholder policy:

- Native PostgreSQL SQL should use `$1`, `$2`, `$3`.
- MySQLOO-compatible `?` placeholders should not be accepted by default.
- PostgreSQL uses `?`, `?|`, and `?&` as real operators, especially for JSON/JSONB, so automatic placeholder translation can corrupt valid PostgreSQL SQL.
- Migration documentation should tell users to replace prepared placeholders with PostgreSQL-native `$1`, `$2`, `$3`.

### Transaction Methods

Transactions inherit query lifecycle methods and add:

```lua
transaction:addQuery(query)
transaction:getQueries()
transaction:clearQueries()
```

Convenience methods may be provided through a Lua helper layer:

```lua
transaction:Query(sql)
transaction:Prepare(sql, values)
transaction:Start(callback, ...)
```

Callbacks:

```lua
function transaction:onError(err) end
function transaction:onSuccess(results) end
function transaction:onAborted() end
```

Transaction staging and execution model:

- `transaction:addQuery(query)` stages query objects only. It does not send SQL to PostgreSQL.
- `transaction:start()` queues the transaction as one module-level job.
- The worker opens one PostgreSQL transaction, sends each staged query to PostgreSQL in order, stores each child query's result, then commits.
- If a child query fails before commit, the worker rolls back and calls the transaction error callback.
- Child query callbacks are not fired individually while the transaction runs, matching MySQLOO.

PostgreSQL-native transaction additions should be considered after baseline compatibility:

```lua
transaction:savepoint(name)
transaction:rollbackTo(name)
transaction:releaseSavepoint(name)
transaction:setIsolationLevel(level)
```

## MySQL-Specific Interface Decisions

### `lastInsert()`

MySQL has a connection-scoped last insert id. PostgreSQL does not. Users should use:

```sql
INSERT INTO table_name (...) VALUES (...) RETURNING id
```

Plan:

- Implement `query:lastInsert()` as a thrown Lua error.
- Error message: `pg: lastInsert() is MySQL-specific and is not supported; use INSERT ... RETURNING instead`.
- Document this as intentionally non-compatible.
- Do not query sequence state implicitly.

### `setMultiStatements()`

MySQLOO uses this to enable or disable multi-statement MySQL behavior.

PostgreSQL can execute multiple statements in one SQL string through the simple query protocol, but the current PostgreSQL backend does not expose MySQLOO-compatible multi-result chains. The safe baseline is single-statement execution.

Plan:

- Provide the method for compatibility.
- Default to single-statement-only raw queries.
- `db:setMultiStatements(false)` stores the single-statement-only mode for raw queries.
- `db:setMultiStatements(true)` should throw until the backend can drain and expose all PostgreSQL result sets correctly.
- Route raw `db:query(sql)` execution through PostgreSQL extended execution with zero parameters, such as `PQexecParams` or a libpqxx equivalent, so PostgreSQL enforces one statement.
- Do not manually scan semicolons or parse SQL. PostgreSQL must be the parser.
- If the active libpqxx/libpq execution layer cannot enforce extended single-statement execution, `db:setMultiStatements(false)` should throw immediately instead of pretending to enforce the setting.
- Prepared queries are already single-statement-oriented and should not need separate enforcement.

### `setCachePreparedStatements()`

MySQL prepared statement caching is not the same as PostgreSQL server-side prepared statements. PostgreSQL prepared statement caching should be optional optimization state only, never correctness state.

Plan:

- Provide the method.
- If the module has a local SQL-to-prepared-name cache, use this method to enable or disable that cache.
- If there is no cache, throw clearly instead of pretending to change behavior.
- Prepared query objects should keep enough durable client-side data, such as SQL text and parameters, to prepare again on a new connection when needed.
- Losing a cache entry must not make a prepared query fail by itself. It should only mean the statement gets prepared again.

### `setAutoReconnect()`

Automatic reconnect should follow MySQLOO's observable behavior without trying to rebuild PostgreSQL session state. It is a transport recovery feature, not a guarantee that temp tables, advisory locks, LISTEN state, transaction state, cursors, or user-issued session settings survive a disconnect.

Plan:

- Provide the method.
- Default to enabled. Networks and database processes are not perfectly reliable, and the module should recover from clearly retriable connection failures where doing so is safe.
- Match MySQLOO compatibility behavior by not re-firing `onConnected` for internal reconnects.
- Add PostgreSQL-native `db:onReconnect()` and `db:onReconnectFailed(err)` callbacks for applications that want observability.
- Reconnect before starting a queued query if the connection is known dead.
- If a queued operation fails with a retriable connection error after execution has started, reconnect for future work but do not replay that operation automatically.
- This intentionally diverges from MySQLOO's one-shot replay behavior because PostgreSQL writes may have reached the server before the client observed the connection failure.
- Treat a transaction as one queued operation for scheduling and callbacks, but do not automatically replay it after connection loss.
- Define retriable connection errors primarily by PostgreSQL SQLSTATE class `08` connection exceptions, using libpq/libpqxx error metadata where available.
- Exclude SQLSTATE `08007` (`transaction_resolution_unknown`) from automatic retry unless a future explicit compatibility mode chooses to match MySQLOO more aggressively.
- Use connection status fallbacks when SQLSTATE is unavailable, such as `PQstatus(conn) == CONNECTION_BAD`, a broken-connection exception type, or the connection object reporting closed after failure.
- Do not treat normal SQL errors as reconnect-retryable, including syntax errors (`42xxx`), constraint violations (`23xxx`), auth failures (`28P01`), deadlocks (`40P01`), or serialization failures (`40001`). Those may be application-level retry cases, not reconnect cases.
- Do not add extra commit-outcome detection or transaction replay machinery in the first implementation. Document that connection loss around commit can have an unknown outcome.
- Do not proactively rebuild session state after reconnect.
- Do not proactively rebuild optional prepared statement caches. Dropping them on reconnect is fine; prepared queries should prepare lazily again from their SQL text when executed.
- Reapply only the connection options required to establish the replacement connection, such as host, credentials, database, port, timeout, and SSL parameters.

### `setSSLMode()` and `setSSLSettings()`

Plan:

- `db:setSSLMode(mode)` is a PostgreSQL-native security API, not a best-effort MySQL compatibility shim.
- Map SSL mode constants strictly to PostgreSQL `sslmode` values:
  - `SSL_MODE_DISABLED` -> `disable`
  - `SSL_MODE_PREFERRED` -> `prefer`
  - `SSL_MODE_REQUIRED` -> `require`
  - `SSL_MODE_VERIFY_CA` -> `verify-ca`
  - `SSL_MODE_VERIFY_IDENTITY` -> `verify-full`
- Reject unknown SSL modes with a Lua error.
- Store SSL mode before connection and apply it to the connection string/options used for initial connect and reconnect.
- `db:setSSLSettings(...)` should map only settings with exact PostgreSQL/libpq equivalents:
  - `key` -> `sslkey`
  - `cert` -> `sslcert`
  - `ca` -> `sslrootcert`
- Unsupported MySQL-style settings, such as `capath` or `cipher` when there is no exact libpq equivalent, must throw instead of silently degrading security.
- Do not silently downgrade from `verify-full` or `verify-ca` if certificate files are missing or invalid. Let libpq fail the connection.

### Deprecated/Unused Options

Plan:

- `OPTION_NUMERIC_FIELDS`: implement.
- `OPTION_NAMED_FIELDS`: export for compatibility, no-op.
- `OPTION_INTERPRET_DATA`: export for compatibility, no-op.
- `OPTION_CACHE`: export for compatibility, no-op or tie to prepared statement cache if implemented.
- `prepared:putNewParameters()`: throw until PostgreSQL multi-result access is implemented; executing hidden batches without exposing results is misleading.

## PostgreSQL-Native Features To Preserve Or Add

These should be available through `pg` without being forced into MySQLOO semantics:

### Connection Strings / DSNs

Implementation direction:

- Accept a PostgreSQL connection string directly in `pg.connect(connectionString)` and `pg.new_connection(connectionString)`.
- Pass DSNs to libpq/libpqxx instead of parsing them manually in the module.
- Validate user-provided DSNs with libpq connection parsing where available so invalid options fail early with PostgreSQL's own error text.
- Keep MySQLOO-style positional connection arguments as a compatibility constructor, but internally convert them into normal PostgreSQL connection options.

### Full libpq Connection Options

Implementation direction:

- Accept table-based connection options using libpq option names, for example `host`, `hostaddr`, `port`, `dbname`, `user`, `password`, `connect_timeout`, `application_name`, `sslmode`, `sslcert`, `sslkey`, and `sslrootcert`.
- Build a libpq connection string from the option table using correct value escaping, not string concatenation with raw values.
- Preserve unknown-but-valid libpq options where possible instead of maintaining a narrow module-specific option list.
- Reject options that cannot be represented safely.

### `$1`-Style Prepared Parameters

Implementation direction:

- Treat `$1`, `$2`, `$3` placeholders as the PostgreSQL-native prepared query format.
- Execute parameters through PostgreSQL parameter APIs, such as libpqxx parameter support or direct `PQexecParams` / prepared statement APIs.
- Never implement native parameters by manually interpolating escaped strings into SQL.
- Do not translate `?` placeholders; users must migrate prepared SQL to PostgreSQL-native placeholders.

### `RETURNING`-Based Inserts

Implementation direction:

- Do not replace PostgreSQL `RETURNING` with a MySQL-style `lastInsert()` abstraction.
- Return `RETURNING` rows through normal query result data.
- Document insert examples using `INSERT ... RETURNING id` for generated integer IDs, UUIDs, computed values, and upserts.
- Make `query:lastInsert()` throw with guidance to use `RETURNING`.

### Transactions With Savepoints

Implementation direction:

- Keep MySQLOO-compatible staged transactions as the baseline transaction API.
- Add PostgreSQL-native savepoint operations after baseline transactions work.
- Prefer explicit methods such as `transaction:savepoint(name)`, `transaction:rollbackTo(name)`, and `transaction:releaseSavepoint(name)`.
- Quote savepoint identifiers safely using PostgreSQL identifier quoting.
- Do not expose long-lived interactive transactions until the staged transaction model is stable and documented.

### `LISTEN` / `NOTIFY`

Implementation direction:

- Add a PostgreSQL-native listener API instead of forcing notifications through query callbacks.
- Provide methods such as `db:listen(channel, callback)`, `db:unlisten(channel)`, and `db:notify(channel, payload)`.
- Deliver notification callbacks on the Lua thread through the same callback-drain mechanism as query completion.
- Use PostgreSQL identifier quoting for channel names.
- Prefer a dedicated listener connection or listener object so notification polling does not block the query worker.
- If listener reconnect support is added, make it part of the listener object that owns the subscriptions rather than a general promise to rebuild arbitrary session state.

### `COPY FROM` / `COPY TO`

Implementation direction:

- Add native bulk import/export APIs instead of modeling `COPY` as a normal MySQLOO query.
- Start with text-mode `COPY` because it is easier to inspect and debug.
- Provide `db:copyFrom(sql, source)` for bulk input and `db:copyTo(sql, callback)` or a streaming object for bulk output.
- Run `COPY` work on the database worker thread and deliver completion/error callbacks on the Lua thread.
- Add binary `COPY` only after text-mode behavior and error handling are stable.

### JSON / JSONB Support

Implementation direction:

- Return JSON and JSONB columns as strings by default so no data is lost and no Lua JSON library is required in C++.
- Add convenience helpers for binding JSON strings to parameters, such as `prepared:setJson(index, jsonString)`.
- Lua helper code may optionally integrate with Garry's Mod `util.TableToJSON` / `util.JSONToTable`, but the C++ core should not require JSON decoding.
- Preserve PostgreSQL's distinction between `json` and `jsonb` when type metadata is exposed.

### Arrays

Implementation direction:

- Return PostgreSQL arrays as strings by default for correctness.
- Add opt-in array decoding only if it can use a reliable PostgreSQL-aware parser, such as libpqxx array parsing support where available.
- Provide `prepared:setArray(index, values)` later as a native helper that serializes Lua tables into PostgreSQL array parameters safely.
- Do not implement array support by naive comma-splitting.

### UUID Values

Implementation direction:

- Return UUID values as strings.
- Bind UUID parameters as strings and let PostgreSQL validate/cast them.
- Optionally add `prepared:setUuid(index, value)` as a validation/documentation helper, not a separate Lua type.

### Timestamp / Date / Time Values

Implementation direction:

- Return date/time values as PostgreSQL text strings by default, preserving timezone and precision as sent by PostgreSQL.
- Avoid guessing local timezone conversions in C++.
- Add opt-in Lua helpers for converting known timestamp formats if needed.
- Document that applications needing exact temporal behavior should choose explicit SQL casts and timezone settings.

### `bytea` Support

Implementation direction:

- Return `bytea` as Lua binary strings.
- Use PostgreSQL/libpq bytea escaping and unescaping APIs, not text hacks.
- Add `prepared:setBinary(index, bytes)` for binary parameters.
- Ensure embedded NUL bytes are preserved by always using string length-aware Lua push/get APIs.

### Notice And Warning Callbacks

Implementation direction:

- Expose PostgreSQL notices separately from query errors.
- Provide database-level callbacks such as `db:onNotice(message, severity, sqlstate)` and `db:onWarning(message, severity, sqlstate)` if severity separation is practical.
- Capture notices through libpq notice receiver/processor hooks where available.
- Queue notice callbacks back to the Lua thread; do not call Lua directly from libpq worker context.

### Cancellation Through PostgreSQL Cancellation APIs

Implementation direction:

- `query:abort()` should remove waiting queries from the module queue when they have not started.
- For a running query, use PostgreSQL cancellation APIs such as `PQcancel` / `PQcancelCreate` depending on available libpq version.
- Report cancelled running queries through `onAborted` when the cancellation is confirmed.
- Keep cancellation best-effort: if the query finishes before cancellation reaches PostgreSQL, deliver the actual query result.

### Explicit Server Parameters And Runtime Settings

Implementation direction:

- Expose read-only connection parameter status through a method such as `db:parameterStatus(name)` backed by libpq where possible.
- Add native helpers for runtime settings using PostgreSQL functions like `current_setting(name, missing_ok)` and `set_config(name, value, is_local)`.
- Prefer parameterized calls to `current_setting` and `set_config` over constructing `SET` SQL by string concatenation.
- Support transaction-local settings through transaction APIs later, using `set_config(name, value, true)` inside the transaction.

## Architecture Plan

The architecture owner for this refactor is the MySQLOO runtime shape: Lua object wrappers, per-database worker queue, query data objects, Think-hook callback delivery, and staged transactions should be adapted from MySQLOO. The PostgreSQL backend owns execution details and must replace the MySQL backend rather than wrapping it.

Target layout:

```text
src/lua/       MySQLOO-derived Lua binding/object layer, renamed for pg
src/postgres/  PostgreSQL backend replacing src/mysql
```

The current `pg/src` code can be used as a reference for existing libpqxx usage and build integration, but it should not remain the runtime model if it duplicates or conflicts with the MySQLOO-derived architecture.

Licensing tasks:

- Preserve MySQLOO's LGPL-2.1 license text in the repository.
- Mark copied/adapted MySQLOO-derived files as LGPL-2.1-derived.
- Keep original MIT license notices for original `gmsv_pg` code that remains.
- Document in the README that the module contains LGPL-2.1-derived MySQLOO compatibility/runtime code.
- Keep source available when distributing rebuilt binaries.

### 1. Replace Raw Pointer Ownership

Current `gmsv_pg` code stores raw pointers for connections, work objects, and threads. MySQLOO's newer runtime already uses stronger ownership patterns in many places, so prefer adapting that model rather than incrementally patching the old `pg/src` model.

Tasks:

- Use RAII for database connections and query state.
- Replace raw `std::thread*` with `std::thread` or a worker abstraction.
- Avoid storing pointers to stack-owned libpqxx objects.
- Define clear object lifetime rules for Lua tables and C++ state.

### 2. Introduce PostgreSQL Backend Types

Create PostgreSQL backend classes matching MySQLOO's existing runtime seams:

```text
PgDatabase
PgQuery
PgPreparedQuery
PgTransaction
PgResultSet
PgQueryData
PgWorkerQueue
```

Responsibilities:

- `PgDatabase`: PostgreSQL connection state, reconnect, per-database queue, connect/disconnect, options.
- `PgQuery`: SQL text, status, result data, error data, cancellation.
- `PgPreparedQuery`: SQL text, parameter storage, PostgreSQL parameter execution.
- `PgTransaction`: ordered set of staged queries executed in one PostgreSQL transaction.
- `PgResultSet`: rows, columns, affected rows, command status, type metadata.
- `PgWorkerQueue`: async execution and callback delivery handoff, adapted from MySQLOO's database worker model.

Do not keep MySQL backend names in the PostgreSQL implementation. Compatibility is at the Lua API boundary; backend code should say PostgreSQL/PG, not MySQL.

### 3. Implement Status Model

Use MySQLOO-compatible statuses:

```text
DATABASE_CONNECTED
DATABASE_CONNECTING
DATABASE_NOT_CONNECTED
DATABASE_CONNECTION_FAILED

QUERY_NOT_RUNNING
QUERY_WAITING
QUERY_RUNNING
QUERY_COMPLETE
QUERY_ABORTED
```

Status transitions must be deterministic and testable.

### 4. Implement Callback Delivery

Use a Think hook model similar to MySQLOO:

- Worker threads enqueue completed callback data.
- Main Lua thread drains completions on `Think`.
- Database objects are tracked in a weak table or equivalent registry.
- Lua callbacks are referenced before async work starts.
- Callback errors are reported without breaking the worker.

Callbacks must be called on the Lua thread only.

### 5. Implement Query Queue

Each database should own its own queue.

Tasks:

- Remove the current global query queue.
- Preserve order per database.
- Support `queueSize()`.
- Support aborting waiting queries.
- Define behavior for aborting running queries using PostgreSQL cancellation.
- Support `wait(shouldSwap)`.

### 6. Implement Result Conversion

Baseline MySQLOO-compatible result shape:

```lua
{
  { column_name = value },
  { column_name = value },
}
```

With `OPTION_NUMERIC_FIELDS`:

```lua
{
  { value1, value2, value3 },
  { value1, value2, value3 },
}
```

Conversion policy:

- SQL `NULL` -> Lua `nil`.
- PostgreSQL booleans -> Lua boolean.
- Integer/float numeric types -> Lua number when safe.
- Large integers that may exceed Lua numeric precision should be configurable.
- Text-like values -> Lua string.
- JSON/JSONB -> string first, optional decoded mode later.
- Arrays -> string first, native table mode later.
- `bytea` -> binary string.

### 7. Implement Prepared Query Parameter Storage

Store parameters by 1-based index to match MySQLOO.

Parameter types:

```text
number
string
boolean
null
binary
json
array
```

Tasks:

- Implement `setNumber`, `setString`, `setBoolean`, `setNull`, `clearParameters`.
- Validate indexes are greater than zero.
- Execute using PostgreSQL parameters, not manual string interpolation.
- Support native `$n` placeholders.
- Do not translate MySQLOO `?` placeholders because that conflicts with PostgreSQL operators.

### 8. Implement Transactions

Tasks:

- `db:createTransaction()` returns transaction object.
- `transaction:addQuery(query)` stages query objects and their prepared parameters without sending SQL.
- `transaction:start()` queues the transaction as one scheduler/retry operation.
- The transaction worker opens one PostgreSQL transaction, sends staged queries one by one in order, stores each child query's result, and commits at the end.
- On any failure, roll back and call `onError`.
- On success, commit and call `onSuccess`.
- Query callbacks inside a transaction should not be called individually, matching MySQLOO.
- `transaction:getQueries()` returns the Lua query objects.
- `transaction:clearQueries()` clears staged work before start.

### 9. Keep Compatibility Helpers In Lua Where Possible

High-level helpers from `mysqloolib.lua` can be implemented in Lua instead of C++:

```lua
pg.ConvertDatabase(database)
pg.CreateDatabase(...)
db:RunQuery(sql, callback, ...)
db:PrepareQuery(sql, values, callback, ...)
db:CreateTransaction()
transaction:Query(sql)
transaction:Prepare(sql, values)
transaction:Start(callback, ...)
```

This keeps the C++ API smaller and makes compatibility behavior easier to iterate.

## Implementation Phases

### Phase 0: Import MySQLOO Runtime Base

- Vendor or copy the MySQLOO source files needed for the Lua object model, query lifecycle, callback delivery, queue, and transaction staging.
- Rename module-facing symbols from `mysqloo` to `pg` while keeping compatibility method names.
- Replace MySQL-specific namespaces, filenames, and class names in the backend slice with PostgreSQL names.
- Remove MySQL client dependency from the new module path.
- Add LGPL-2.1 license documentation for derived files.
- Keep the build producing `gmsv_pg_*` binaries.

Exit criteria:

- The imported runtime compiles as `pg` without linking MySQL.
- PostgreSQL backend stubs exist for database/query/prepared query/transaction execution.
- No MySQL backend code remains in the active build except as reference material outside the compiled source set.

### Phase 1: API Skeleton

- Expose MySQLOO-compatible constants from the `pg` module table.
- Add `pg.connect(...)` constructor using MySQLOO's public shape but PostgreSQL connection options internally.
- Keep `pg.new_connection(...)` only if it remains a useful PostgreSQL-native alias.
- Ensure database methods match MySQLOO shape through the MySQLOO-derived Lua binding layer.
- Add no-op compatibility methods only where the plan explicitly allows them.
- Preserve MySQLOO callback-field lookup as the primary compatibility path.

Exit criteria:

- Existing simple MySQLOO code can create a database object and call `db:query(sql)`.
- Module still builds with Premake 4.

### Phase 2: Query Lifecycle

- Replace `run()` with `start()` while keeping `run()` as an alias if desired.
- Implement query statuses.
- Implement `getData`, `error`, `isRunning`, `wait`, `abort`, `affectedRows`.
- Implement `onSuccess`, `onError`, `onData`, `onAborted` callback fields.
- Implement per-database queue.

Exit criteria:

- Basic MySQLOO query examples work against PostgreSQL SQL.

### Phase 3: Result Sets And Options

- Implement stable result storage.
- Implement `OPTION_NUMERIC_FIELDS`.
- Add initial type conversion policy.
- Keep public `hasMoreResults()` false and `getNextResults()` unsupported until PostgreSQL multi-result chains are implemented correctly.

Exit criteria:

- `query:getData()` works after completion.
- `query:onData(row)` fires per row.
- Numeric field option changes row shape.

### Phase 4: Prepared Queries

- Rework prepared query internals.
- Implement parameter setters.
- Implement native `$n` parameters.
- Reject/document MySQLOO `?` placeholder migration as a SQL change users must make.
- Fix lifetime issues in current prepared query code.

Exit criteria:

- PostgreSQL-native `db:prepare("SELECT $1")` works without translation.

### Phase 5: Transactions

- Implement transaction object.
- Execute staged queries atomically.
- Implement transaction callbacks.
- Add convenience Lua helpers.

Exit criteria:

- MySQLOO transaction example can be ported by changing SQL syntax only.

### Phase 6: PostgreSQL Features

- Add `RETURNING` examples and documentation.
- Add `LISTEN` / `NOTIFY` API.
- Add `COPY` API.
- Add PostgreSQL-specific type helpers.
- Add notice/warning callbacks.
- Add savepoint support.

Exit criteria:

- The module is not just MySQLOO-shaped; it exposes PostgreSQL strengths.

### Phase 7: Compatibility Documentation

- Document the source-level migration path from `mysqloo` references to `pg` references.
- Document SQL dialect limitations.
- Document unsupported or changed MySQL-specific methods.
- Provide side-by-side examples.
- Provide a migration checklist.

Exit criteria:

- Users can understand which MySQLOO code works unchanged and which code needs PostgreSQL SQL changes.

## Tests And Verification

### Build Verification

- `cd pg && premake4 gmake`
- `cd pg/project && make -B`
- Verify output is 32-bit Linux module where applicable.

### Lua Interface Tests

Create integration-style Lua examples similar to MySQLOO:

- `database.lua`
- `query.lua`
- `prepared_query.lua`
- `transactions.lua`
- `multi_results.lua` if supported
- `compat_mysqloo_replacement.lua`

### Database Tests

Use a local PostgreSQL test database.

Test cases:

- Successful async connect.
- Failed async connect.
- Disconnect callback.
- Raw select.
- Insert/update/delete affected rows.
- `RETURNING` id example.
- Query error with SQL string in callback.
- Abort waiting query.
- Wait for query.
- Prepared string/number/bool/null parameters.
- Transaction commit.
- Transaction rollback on error.
- Numeric field option.
- `NULL` conversion.
- Boolean conversion.

### Compatibility Tests

Run adapted MySQLOO examples after replacing `mysqloo` references with `pg` references:

```lua
local pg = require("pg")
local db = pg.connect(...)
```

Expected limitations should be asserted explicitly, not treated as accidental failures.

## Documentation Updates

- README build dependencies are already documented for Linux 32-bit builds.
- Add API reference for PostgreSQL-native API.
- Add MySQLOO compatibility reference.
- Add migration guide.
- Add examples using `RETURNING` instead of `lastInsert()`.
- Add prepared statement examples using PostgreSQL `$1` placeholders.

## Open Questions

- Should the module table be named only `pg`, or should there be an opt-in compatibility loader that registers `mysqloo` for legacy deployments?
- Should type conversion default to preserving large integers as strings?
- Should PostgreSQL-native helpers live directly on the same objects or under `pg.native` / `pg.postgres`?
- Should old `conn:query(...):run()` API remain as aliases after the refactor?

## Suggested First PR

The first implementation PR should be small and mechanical:

- Add constants.
- Add `pg.connect(...)` alias/constructor.
- Add `query:start()` alias for current `run()`.
- Add callback-field dispatch for `onSuccess` and `onError` alongside current event emitter.
- Add docs that this is the start of compatibility work.
- Keep behavior otherwise unchanged.

This gives a stable base before replacing the query scheduler and prepared query internals.
