# gmsv_pg
A PostgreSQL adapter for Garry's Mod.

> **Note:** this is a vibe-coded project: it has been developed through heavy AI-assisted iteration and should not be trusted without review/testing like any other native server module before production use.

## Credits and provenance

- Original **gmsv_pg** project/codebase: TeslaCloud Studios, MIT licensed. See `LICENSE.md`.
- **MySQLOO** runtime/callback/object model: FredyH/MySQLOO contributors, LGPL-2.1 licensed. Portions of the Lua runtime layer are derived/adapted from MySQLOO; see `LICENSE_MYSQLOO_LGPL.md`.
- PostgreSQL behavior in this fork/rework is intentionally PostgreSQL-first while keeping a MySQLOO-shaped Lua API for migration ergonomics.

## Installation

This chapter covers installing a prebuilt module into a Garry's Mod server. If you are building from source, see [Building](#building) first, then come back here to install the produced binary.

### 1. Check your server architecture

From the server console, run:

```lua
lua_run print(jit.os, jit.arch)
```

Use the module binary matching that output. Current verified and shipped path is Linux 32-bit Garry's Mod (`Linux x86`). Windows support is inherited from the original project layout but is currently unverified in this fork/rework, and no Windows binary is currently shipped here.

### 2. Install the module binary

1. Create this folder if it does not exist:

   ```text
   garrysmod/lua/bin/
   ```

2. Copy the correct module binary into that folder:

   ```text
   garrysmod/lua/bin/gmsv_pg_linux.dll   # Linux 32-bit
   ```

The built Linux binary is produced at `pg/bin/gmsv_pg_linux.dll`.

### 3. Install runtime dependencies

The module dynamically uses PostgreSQL's client library (`libpq`). The PostgreSQL runtime dependency must be loadable by SRCDS/Garry's Mod.

#### Linux verified path

Install the 32-bit PostgreSQL client development/runtime packages:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install build-essential g++-multilib libc6-dev-i386 linux-libc-dev:i386 libpq-dev:i386
```

Then either rely on the system loader finding the installed 32-bit libraries, or copy the bundled runtime dependency beside the server executable:

```text
runtime_depends/linux/libpq.so.5 -> server root next to srcds_linux
```

The bundled `runtime_depends/linux/libpq.so.5` must match the SSL/runtime libraries available on the host. If the module fails to load with an error like `libssl.so.1.1: cannot open shared object file`, refresh the bundled runtime dependency from the installed 32-bit libpq package:

```sh
cp /lib/i386-linux-gnu/libpq.so.5 runtime_depends/linux/libpq.so.5
```

Fully static libpq linking is not currently used because Debian's `libpq.a` depends on PostgreSQL internal support archives that are not shipped with the normal `libpq-dev:i386` package.

#### Windows unverified path

Windows is not currently verified in this fork/rework. If you try it:

1. Install the Microsoft Visual C++ Redistributable required by your server/module build.
2. Build or otherwise provide a fresh Windows module binary and copy it into `garrysmod/lua/bin/`.
3. Copy the contents of `runtime_depends/windows` to the server root next to `srcds.exe` so PostgreSQL client DLLs can be loaded.

### 4. Verify the module loads

Restart the server and run:

```lua
lua_run require("pg") print(pg.VERSION, pg.MINOR_VERSION)
```

If this fails, check:

- the binary is in `garrysmod/lua/bin/`
- the binary suffix matches `jit.os` / `jit.arch`
- PostgreSQL client runtime libraries are loadable by the server
- on Linux, the 32-bit `libpq` runtime and its SSL dependencies match the host

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

Callback examples use Lua's colon syntax. For example, `function query:onSuccess(rows)` receives the query object as `self` and the result rows as the next argument. The equivalent dot-style signature is `function query.onSuccess(query, rows)`.

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

#### Version fields

```lua
pg.VERSION
pg.MINOR_VERSION
```

String version fields exported for MySQLOO-shaped compatibility.

#### Diagnostic counters

These functions are exposed for debugging/tests and are not needed for normal database code:

```lua
pg.objectCount()
pg.allocationCount()
pg.deallocationCount()
pg.referenceCreatedCount()
pg.referenceFreedCount()
```

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

Blocks until the initial connection attempt completes, then drains callbacks for that database. This freezes the server while waiting; prefer normal asynchronous callbacks unless blocking is truly required.

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

`key`, `cert`, and `ca` are optional. `capath` and `cipher` currently throw because they do not have exact libpq equivalents in this implementation.

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

Blocks until this query execution completes and drains callbacks for the owning database. If `swapToFront` is true, attempts to move the queued query to the front before waiting. This can lag/freeze the server; use asynchronous callbacks in normal gameplay code.

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

String parameters are sent separately from the SQL text; do not pre-escape values before passing them to `query:setString()`.

Prepared query objects can be reused. Change parameters and call `query:start()` again to queue another execution; each execution receives its own parameter snapshot.

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

Transaction objects are one-shot execution units. Calling `tx:start()` more than once throws; create a new transaction object for retries or repeated work.

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

## Full non-exotic example

This script demonstrates the normal supported API surface in one place:

- connection callbacks
- metadata after connect
- raw queries
- prepared queries and reuse
- `RETURNING` instead of `lastInsert()`
- result rows and `onData`
- affected rows
- transactions
- query cancellation
- reconnect callbacks
- graceful disconnect

It intentionally avoids unsupported/exotic features such as multi-result chains, COPY, LISTEN/NOTIFY, notices, command tags, savepoints, and MySQL-only behavior.

```lua
require("pg")

local db = pg.connect({
	host = "127.0.0.1",
	port = 5432,
	dbname = "gmsv_pg_test",
	user = "postgres",
	password = "postgres",
	application_name = "gmsv_pg_readme_example"
})

local function fail(label, err, sql)
	print("[pg example] FAIL", label, err or "", sql or "")
end

local function run(sql, onDone)
	local q = db:query(sql)

	function q:onError(err, failedSql)
		fail("query", err, failedSql)
	end

	function q:onSuccess(rows)
		if onDone then onDone(rows, q) end
	end

	q:start()
	return q
end

local function prepareUserInsert()
	local q = db:prepare("INSERT INTO pg_example_users(name, admin) VALUES($1, $2) RETURNING id, name, admin")

	function q:onData(row)
		print("[pg example] inserted row", row.id, row.name, row.admin)
	end

	function q:onError(err, failedSql)
		fail("prepared insert", err, failedSql)
	end

	return q
end

local function insertUsers(onDone)
	local insert = prepareUserInsert()
	local inserted = {}

	local function insertOne(name, admin, done)
		insert:clearParameters()
		insert:setString(1, name) -- Do not pre-escape prepared parameters.
		insert:setBoolean(2, admin)

		function insert:onSuccess(rows)
			inserted[#inserted + 1] = rows[1]
			print("[pg example] affected rows", insert:affectedRows())
			done()
		end

		insert:start()
	end

	insertOne("alice", true, function()
		insertOne("bob", false, function()
			onDone(inserted)
		end)
	end)
end

local function runTransaction(onDone)
	local tx = db:createTransaction()

	tx:addQuery(db:query("INSERT INTO pg_example_audit(message) VALUES('transaction started') RETURNING id, message"))

	local insert = db:prepare("INSERT INTO pg_example_users(name, admin) VALUES($1, $2) RETURNING id, name, admin")
	insert:setString(1, "carol")
	insert:setBoolean(2, false)
	tx:addQuery(insert)

	function tx:onError(err)
		fail("transaction", err)
	end

	function tx:onSuccess(results)
		print("[pg example] transaction audit id", results[1][1].id)
		print("[pg example] transaction user", results[2][1].name)
		onDone()
	end

	tx:start()
end

local function demonstrateCancel(onDone)
	local slow = db:query("SELECT pg_sleep(5)")

	function slow:onSuccess()
		fail("cancel", "slow query unexpectedly succeeded")
	end

	function slow:onError(err, failedSql)
		fail("cancel", err, failedSql)
	end

	function slow:onAborted()
		print("[pg example] slow query aborted")
		onDone()
	end

	slow:start()
	timer.Simple(0.2, function()
		print("[pg example] abort requested", slow:abort())
	end)
end

function db:onConnected()
	print("[pg example] connected")
	print("[pg example] server version", db:serverVersion())
	print("[pg example] server info", db:serverInfo())
	print("[pg example] host info", db:hostInfo())

	run("DROP TABLE IF EXISTS pg_example_audit", function()
		run("DROP TABLE IF EXISTS pg_example_users", function()
			run("CREATE TABLE pg_example_users (id serial PRIMARY KEY, name text NOT NULL, admin boolean NOT NULL)", function()
				run("CREATE TABLE pg_example_audit (id serial PRIMARY KEY, message text NOT NULL)", function()
					insertUsers(function()
						runTransaction(function()
							run("SELECT id, name, admin FROM pg_example_users ORDER BY id", function(rows)
								for _, row in ipairs(rows) do
									print("[pg example] user", row.id, row.name, row.admin)
								end

								demonstrateCancel(function()
									db:disconnect(true)
								end)
							end)
						end)
					end)
				end)
			end)
		end)
	end)
end

function db:onConnectionFailed(err)
	fail("connect", err)
end

function db:onDisconnected()
	print("[pg example] disconnected")
end

function db:onReconnect()
	print("[pg example] reconnected for future work")
end

function db:onReconnectFailed(err)
	fail("reconnect", err)
end

db:setAutoReconnect(true)
db:connect()
```
