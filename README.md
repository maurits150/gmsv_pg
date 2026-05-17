# gmsv_pg
A PostgreSQL adapter for Garry's Mod.

## Installation
The pre-compiled binaries are located in the `releases` section of this repo.

Copy-paste the `gmsv_pg_*.dll` to your server's `lua/bin` folder, where `*` if your platform's suffix (win32 or linux). Then follow the platform-specific instructions below:

### Windows
1. Make sure you have Microsoft Visual C++ 2015 Redist installed
2. Navigate to the `runtime_depends/windows` folder
3. Copy-paste the folder's contents to your server's root (where srcds.exe is)

### Linux
On Linux there are two ways to install dependencies.

**via apt**
```sh
# make sure you have postgresql repositories added beforehand!
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install build-essential g++-multilib libc6-dev-i386 linux-libc-dev:i386 libpq-dev:i386
```

**the lazy way**
1. Navigate to `runtime_depends/linux`
2. Copy-paste the `libpq.so.5` file to your server's root (where srcds_linux is)

Please note that even if you do the "lazy way" it might still be necessary to `apt-get` the `libpq-dev:i386` package. If you run into issues, please make sure that it's the first step that you take before complaining.

The bundled `runtime_depends/linux/libpq.so.5` must match the SSL/runtime libraries available on the host. If the module fails to load with an error like `libssl.so.1.1: cannot open shared object file`, refresh the bundled runtime dependency from the installed 32-bit libpq package:

```sh
cp /lib/i386-linux-gnu/libpq.so.5 runtime_depends/linux/libpq.so.5
```

Fully static libpq linking is not currently used because Debian's `libpq.a` depends on PostgreSQL internal support archives that are not shipped with the normal `libpq-dev:i386` package.

## Building

Linux builds target Garry's Mod's 32-bit server module ABI, so install 32-bit build dependencies first:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install premake4 build-essential g++-multilib libc6-dev-i386 linux-libc-dev:i386 libpq-dev:i386
```

Generate makefiles and build:

```sh
cd pg
premake4 gmake
cd project
make
```

The built module is written to:

```text
pg/bin/gmsv_pg_linux.dll
```

For local testing or deployment, copy or symlink that file into your server's `garrysmod/lua/bin/` folder and copy `runtime_depends/linux/libpq.so.5` into the server root/bin location described above.

## Usage
This module doesn't have all of the features implemented yet, it's being worked on.

The module now uses a PostgreSQL-first MySQLOO-shaped API. The runtime/callback layer is derived from MySQLOO and is covered by LGPL-2.1; see `LICENSE_MYSQLOO_LGPL.md`.

### Current API shape

```lua
require("pg")

local db = pg.connect("127.0.0.1", "postgres", "password", "database", 5432)

-- Native PostgreSQL connection strings are supported.
local dbFromDsn = pg.connect("host=127.0.0.1 port=5432 dbname=database user=postgres password=password")

-- Native libpq option tables are supported.
local dbFromOptions = pg.connect({
	host = "127.0.0.1",
	port = 5432,
	dbname = "database",
	user = "postgres",
	password = "password",
	application_name = "gmsv_pg"
})

function db:onConnected()
	local query = db:query("SELECT 1 AS value")

	function query:onSuccess(data)
		print(data[1].value)
	end

	function query:onError(err, sql)
		print(err, sql)
	end

	query:start()
end

function db:onConnectionFailed(err)
	print(err)
end

db:connect()
```

Prepared queries use PostgreSQL-native placeholders. MySQLOO `?` placeholders are intentionally not translated because `?`, `?|`, and `?&` are valid PostgreSQL operators.

```lua
local query = db:prepare("SELECT $1::int AS value")
query:setNumber(1, 42)
query:start()
```

Use PostgreSQL `RETURNING` for generated IDs:

```sql
INSERT INTO users (name) VALUES ($1) RETURNING id
```

`query:lastInsert()` intentionally throws because PostgreSQL has no MySQL-style connection-scoped insert id.

## API reference

The API is MySQLOO-shaped, but SQL and connection behavior are PostgreSQL-native. Unsupported MySQL-only compatibility methods throw instead of pretending to work.

### Module

#### `pg.connect(host, user, password, database, port, unixSocket)`

Creates a database object. `port` defaults to `5432`; `unixSocket` may be used instead of a TCP host.

#### `pg.connect(connectionString)`

Creates a database object from a libpq connection string, for example:

```lua
local db = pg.connect("host=127.0.0.1 port=5432 dbname=mydb user=postgres password=secret")
```

#### `pg.connect(options)`

Creates a database object from a table of libpq options:

```lua
local db = pg.connect({
	host = "127.0.0.1",
	port = 5432,
	dbname = "mydb",
	user = "postgres",
	password = "secret",
	application_name = "gmsv_pg"
})
```

#### `pg.new_connection(...)`

Alias for `pg.connect(...)`.

### Constants

Database status:

- `pg.DATABASE_CONNECTED`
- `pg.DATABASE_CONNECTING`
- `pg.DATABASE_NOT_CONNECTED`
- `pg.DATABASE_CONNECTION_FAILED`

Query status:

- `pg.QUERY_NOT_RUNNING`
- `pg.QUERY_RUNNING`
- `pg.QUERY_COMPLETE`
- `pg.QUERY_ABORTED`
- `pg.QUERY_WAITING`

Query options:

- `pg.OPTION_NUMERIC_FIELDS` — result rows use numeric column indexes instead of names.
- `pg.OPTION_NAMED_FIELDS` — compatibility constant; named fields are the default.
- `pg.OPTION_INTERPRET_DATA` — compatibility constant; supported PostgreSQL types are interpreted by default.
- `pg.OPTION_CACHE` — compatibility constant; prepared statement caching is not implemented.

SSL mode:

- `pg.SSL_MODE_DISABLED` → `sslmode=disable`
- `pg.SSL_MODE_PREFERRED` → `sslmode=prefer`
- `pg.SSL_MODE_REQUIRED` → `sslmode=require`
- `pg.SSL_MODE_VERIFY_CA` → `sslmode=verify-ca`
- `pg.SSL_MODE_VERIFY_IDENTITY` → `sslmode=verify-full`

### Database object

#### `db:connect()`

Starts the asynchronous connection attempt. Throws if called more than once on the same database object.

Callbacks:

```lua
function db:onConnected() end
function db:onConnectionFailed(err) end
function db:onDisconnected() end
function db:onReconnect() end
function db:onReconnectFailed(err) end
```

Reconnect callbacks are for internal reconnect attempts after an established connection breaks. Failed in-flight statements are not replayed automatically.

#### `db:wait()`

Blocks until the initial connection attempt completes, then drains callbacks for that database.

#### `db:disconnect(wait)`

Starts shutdown. Queued queries are aborted and active PostgreSQL work is cancellation-requested. If `wait` is true, waits for the worker thread to finish and then drains callbacks.

#### `db:status()`

Returns one of the `pg.DATABASE_*` constants.

#### `db:serverVersion()` / `db:serverInfo()` / `db:hostInfo()`

Returns connection metadata after successful connection. These throw before the database is connected.

#### `db:query(sql)`

Creates a raw SQL query object. SQL is PostgreSQL SQL.

#### `db:prepare(sql)`

Creates a prepared query object. Use PostgreSQL placeholders: `$1`, `$2`, etc. MySQLOO/MySQL `?` placeholders are not translated.

#### `db:createTransaction()`

Creates a transaction object.

#### `db:escape(str)`

Escapes a string using the connected PostgreSQL connection.

#### `db:setCharacterSet(charset)`

Runs `SET CLIENT_ENCODING TO ...`. Returns `true, ""` on success for MySQLOO-shaped compatibility.

#### `db:setAutoReconnect(enabled)`

Enables/disables reconnect attempts for future work after connection loss. Defaults to enabled.

#### `db:abortAllQueries()`

Aborts queued queries and requests cancellation for the active running PostgreSQL query, if any. Returns the number of aborts requested/completed.

#### `db:queueSize()`

Returns queued work count. Does not include the active running query.

#### `db:ping()`

Runs `SELECT 1` on the current connection and returns a boolean.

#### Connection options

```lua
db:setSSLMode(pg.SSL_MODE_REQUIRED)
db:setSSLSettings(key, cert, ca, capath, cipher)
db:setConnectTimeout(seconds)
```

`capath` and `cipher` currently throw because they do not have exact libpq equivalents in this implementation.

Unsupported compatibility methods:

```lua
db:setMultiStatements(true)       -- throws
db:setCachePreparedStatements(x)  -- throws
db:setReadTimeout(seconds)        -- throws; use PostgreSQL statement_timeout
db:setWriteTimeout(seconds)       -- throws
```

### Query object

#### `query:start()`

Queues the query for asynchronous execution.

Callbacks:

```lua
function query:onSuccess(rows) end
function query:onData(row) end
function query:onError(err, sql) end
function query:onAborted() end
```

`onData` is called once per result row before `onSuccess`.

#### `query:wait(swapToFront)`

Blocks until this query execution completes and drains callbacks for the owning database. If `swapToFront` is true, attempts to move the queued query to the front before waiting.

#### `query:abort()`

Aborts a queued query or requests PostgreSQL cancellation for the active running query. Returns true if an abort/cancel was requested or completed.

#### `query:isRunning()`

Returns true when the query has queued/running executions.

#### `query:error()`

Returns the last callback error string, or an empty string.

#### `query:setOption(option, enabled)`

Sets query options. `pg.OPTION_NUMERIC_FIELDS` is the meaningful option today.

#### `query:getData()`

Returns the current/last result rows for successful query data, or `nil` on error/no data.

Rows are Lua tables. By default fields are named by column name. With `OPTION_NUMERIC_FIELDS`, fields use 1-based numeric indexes.

#### `query:affectedRows()`

Returns PostgreSQL affected rows for the current result.

#### `query:oid()`

Returns PostgreSQL inserted OID when available; usually `0` for normal modern tables.

Unsupported compatibility methods:

```lua
query:lastInsert()      -- throws; use INSERT ... RETURNING
query:commandStatus()   -- throws until direct libpq command tags are implemented
query:hasMoreResults()  -- false until multi-result chains are implemented
query:getNextResults()  -- throws until multi-result chains are implemented
```

### Prepared query object

Prepared queries inherit all query methods and add parameter setters:

```lua
local q = db:prepare("INSERT INTO users(name, admin) VALUES($1, $2) RETURNING id")
q:setString(1, "alice")
q:setBoolean(2, false)
q:start()
```

Methods:

- `query:setNumber(index, value)`
- `query:setString(index, value)`
- `query:setBoolean(index, value)`
- `query:setNull(index)`
- `query:clearParameters()`

Indexes are positive integers. Values are snapshotted when `query:start()` or `transaction:addQuery(query)` builds an execution.

Unsupported:

```lua
query:putNewParameters() -- throws until real multi-result parameter batches exist
```

### Transaction object

Transactions stage existing query/prepared-query objects and run them inside one PostgreSQL transaction.

```lua
local tx = db:createTransaction()
tx:addQuery(db:query("INSERT INTO audit(message) VALUES('begin')"))

local q = db:prepare("INSERT INTO users(name) VALUES($1) RETURNING id")
q:setString(1, "alice")
tx:addQuery(q)

function tx:onSuccess(results)
	-- results[1], results[2], ... are per-child query result tables
end

function tx:onError(err)
	-- transaction rolled back
end

tx:start()
```

Methods:

- `tx:addQuery(query)` — stages a query using its current execution data.
- `tx:getQueries()` — returns a Lua snapshot table of staged query objects.
- `tx:clearQueries()` — clears staged queries.
- `tx:start()`, `tx:wait()`, `tx:abort()`, `tx:isRunning()`, `tx:error()` — inherited query methods.

Child query callbacks are suppressed inside transactions. The transaction callback fires once after commit or rollback.

### Result conversion

Current automatic conversions:

- PostgreSQL `bool` → Lua boolean
- PostgreSQL `int2`, `int4`, `float4`, `float8` → Lua number
- PostgreSQL `bytea` → Lua binary string
- SQL `NULL` → Lua `nil`
- Other types, including `int8` and `numeric`, → Lua string to avoid precision loss

### PostgreSQL-first migration notes

- Replace `mysqloo` construction/callback patterns with `pg`, but keep SQL PostgreSQL-native.
- Use `$1`, `$2`, ... placeholders, not `?`.
- Use `RETURNING` instead of `lastInsert()`.
- Do not rely on automatic replay of failed statements after reconnect.
- Do not use multi-statement/multi-result MySQL behavior until native PostgreSQL multi-result support exists.
