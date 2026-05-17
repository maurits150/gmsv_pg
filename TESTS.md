# gmsv_pg Test Suite

This file defines the module test suite for local development. Tests are designed to run against:

- A Docker PostgreSQL container.
- A running Garry's Mod instance with the `lua/autorun/dev_eval.lua` bridge loaded.
- The built `gmsv_pg_linux.dll` symlinked or copied into `garrysmod/lua/bin/`.
- `runtime_depends/linux/libpq.so.5` copied to the Garry's Mod root or `bin/` folder as described in `runtime_depends/readme.md`.

Do not use `coroutine.yield()` in eval snippets. The bridge runs snippets through a normal eval call stack, so yielding can fail with `attempt to yield across C-call boundary`. Start async work in one eval, write progress/results to a temporary global, then fetch that global with a second eval.

## 1. Build The Module

```sh
cd /home/maurits/projects/gmsv_pg/pg
premake4 gmake
cd project
make -B
```

Expected output:

```text
Linking pg
```

Verify the module shape:

```sh
cd /home/maurits/projects/gmsv_pg
file pg/bin/gmsv_pg_linux.dll
ldd pg/bin/gmsv_pg_linux.dll | grep libpq
```

Expected:

- `ELF 32-bit LSB shared object`
- `libpq.so.5` resolves or is available through the deployed runtime dependency.

## 2. Start PostgreSQL Test Container

```sh
docker ps --format '{{.Names}}' | grep -qx gmsv-pg-test || \
docker run --name gmsv-pg-test \
  -e POSTGRES_PASSWORD=postgres \
  -e POSTGRES_DB=gmsv_pg_test \
  -p 127.0.0.1:55432:5432 \
  -d postgres:16-alpine

for i in $(seq 1 30); do
  docker exec gmsv-pg-test pg_isready -U postgres -d gmsv_pg_test && break
  sleep 1
done
```

Reset the container when needed:

```sh
docker rm -f gmsv-pg-test
```

## 3. Start Garry's Mod And Verify Bridge

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./relaunch.sh
./wait_for_startup.sh
```

Expected:

```text
server eval ready
client eval ready
server gamemode ready
client gamemode ready
Game ready via eval bridge
```

## 4. Quick Module Load Smoke Test

Run:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")
print("pg table", type(pg))
print("version", pg.VERSION, pg.MINOR_VERSION)
print("constants", pg.DATABASE_NOT_CONNECTED, pg.QUERY_WAITING, pg.SSL_MODE_VERIFY_IDENTITY)
print("constructors", type(pg.connect), type(pg.new_connection))
LUA
```

Expected:

- `pg table table`
- Constructors are both `function`.
- No module load errors.

## 5. Full In-Game Integration Suite

Start the suite:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")

_G.__pgTest = {
	done = false,
	failed = false,
	index = 0,
	lines = {},
	tests = {}
}

local state = _G.__pgTest

local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[#parts + 1] = tostring(select(i, ...))
	end
	local text = table.concat(parts, "\t")
	state.lines[#state.lines + 1] = text
	print("[pg test]", text)
end

local function fail(...)
	state.failed = true
	line("FAIL", ...)
	state.done = true
end

local function pass(...)
	line("PASS", ...)
end

local function assertEqual(name, actual, expected)
	if actual ~= expected then
		fail(name, "expected", expected, "got", actual)
		return false
	end
	return true
end

local function assertTruthy(name, value)
	if not value then
		fail(name, "expected truthy", value)
		return false
	end
	return true
end

local function assertThrows(name, fn, expectedText)
	local ok, err = pcall(fn)
	if ok then
		fail(name, "expected throw")
		return false
	end
	if expectedText and not string.find(tostring(err), expectedText, 1, true) then
		fail(name, "wrong error", err)
		return false
	end
	pass(name, "threw", err)
	return true
end

local function connectDatabase(onReady)
	local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
	function db:onConnected()
		onReady(db)
	end
	function db:onConnectionFailed(err)
		fail("connect", err)
	end
	db:connect()
end

local function runQuery(db, sql, onSuccess)
	local query = db:query(sql)
	function query:onSuccess(rows)
		onSuccess(rows, query)
	end
	function query:onError(err, querySql)
		fail("query error", err, querySql)
	end
	query:start()
	return query
end

local function runPrepared(db, sql, bind, onSuccess)
	local query = db:prepare(sql)
	bind(query)
	function query:onSuccess(rows)
		onSuccess(rows, query)
	end
	function query:onError(err, querySql)
		fail("prepared error", err, querySql)
	end
	query:start()
	return query
end

local function nextTest()
	if state.done then
		return
	end

	state.index = state.index + 1
	local test = state.tests[state.index]
	if not test then
		state.done = true
		pass("suite complete")
		return
	end

	line("START", state.index, test.name)
	test.run(nextTest)
end

state.tests = {
	{
		name = "module table and constants",
		run = function(done)
			if not assertEqual("pg type", type(pg), "table") then return end
			if not assertEqual("connect type", type(pg.connect), "function") then return end
			if not assertEqual("new_connection type", type(pg.new_connection), "function") then return end
			if not assertEqual("not connected constant", pg.DATABASE_NOT_CONNECTED, 2) then return end
			if not assertEqual("waiting constant", pg.QUERY_WAITING, 5) then return end
			pass("module table and constants")
			done()
		end
	},
	{
		name = "constructor forms and pre-connect status",
		run = function(done)
			local positional = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			local dsn = pg.connect("host=127.0.0.1 port=55432 dbname=gmsv_pg_test user=postgres password=postgres")
			local options = pg.connect({ host = "127.0.0.1", port = 55432, dbname = "gmsv_pg_test", user = "postgres", password = "postgres", application_name = "gmsv_pg_tests" })
			if not assertEqual("positional status", positional:status(), pg.DATABASE_NOT_CONNECTED) then return end
			if not assertEqual("dsn status", dsn:status(), pg.DATABASE_NOT_CONNECTED) then return end
			if not assertEqual("options status", options:status(), pg.DATABASE_NOT_CONNECTED) then return end
			pass("constructor forms")
			done()
		end
	},
	{
		name = "unsupported compatibility methods throw",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			assertThrows("setMultiStatements true", function() db:setMultiStatements(true) end, "multi-statement")
			assertThrows("setReadTimeout", function() db:setReadTimeout(1) end, "setReadTimeout")
			assertThrows("setWriteTimeout", function() db:setWriteTimeout(1) end, "setWriteTimeout")
			local query = db:query("SELECT 1")
			assertThrows("lastInsert", function() query:lastInsert() end, "RETURNING")
			done()
		end
	},
	{
		name = "connection failure callback",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 1)
			db:setConnectTimeout(1)
			function db:onConnected()
				fail("connection failure", "unexpected connect")
			end
			function db:onConnectionFailed(err)
				if not assertTruthy("connection error", err and #err > 0) then return end
				pass("connection failure callback", err)
				done()
			end
			db:connect()
		end
	},
	{
		name = "setup schema with single statements",
		run = function(done)
			connectDatabase(function(db)
				state.db = db
				runQuery(db, "DROP TABLE IF EXISTS gmsv_pg_suite", function()
					runQuery(db, "CREATE TABLE gmsv_pg_suite (id serial PRIMARY KEY, name text NOT NULL, active boolean NOT NULL, amount int4, big_value int8, exact_value numeric, payload bytea, maybe text)", function()
						pass("setup schema")
						done()
					end)
				end)
			end)
		end
	},
	{
		name = "metadata after connect",
		run = function(done)
			local db = state.db
			if not assertEqual("status connected", db:status(), pg.DATABASE_CONNECTED) then return end
			if not assertTruthy("server version", db:serverVersion() > 0) then return end
			if not assertTruthy("server info", #db:serverInfo() > 0) then return end
			if not assertTruthy("host info", #db:hostInfo() > 0) then return end
			if not assertEqual("ping", db:ping(), true) then return end
			pass("metadata", db:serverVersion(), db:serverInfo(), db:hostInfo())
			done()
		end
	},
	{
		name = "raw query result conversion",
		run = function(done)
			runQuery(state.db, "SELECT 7::int4 AS amount, 9223372036854775807::int8 AS big_value, 12.34::numeric AS exact_value, true AS active, NULL::text AS maybe, 'hello'::text AS name", function(rows)
				local row = rows and rows[1]
				if not assertTruthy("row exists", row) then return end
				if not assertEqual("int4 number", row.amount, 7) then return end
				if not assertEqual("int8 string", row.big_value, "9223372036854775807") then return end
				if not assertEqual("numeric string", row.exact_value, "12.34") then return end
				if not assertEqual("bool", row.active, true) then return end
				if not assertEqual("null", row.maybe, nil) then return end
				if not assertEqual("text", row.name, "hello") then return end
				pass("raw query conversion")
				done()
			end)
		end
	},
	{
		name = "onData callback per row",
		run = function(done)
			local seen = 0
			local query = state.db:query("SELECT generate_series(1, 3)::int4 AS value")
			function query:onData(row)
				seen = seen + 1
			end
			function query:onSuccess(rows)
				if not assertEqual("onData count", seen, 3) then return end
				if not assertEqual("row count", #rows, 3) then return end
				pass("onData")
				done()
			end
			function query:onError(err) fail("onData query", err) end
			query:start()
		end
	},
	{
		name = "numeric fields option",
		run = function(done)
			local query = state.db:query("SELECT 1::int4 AS one, 'two'::text AS two")
			query:setOption(pg.OPTION_NUMERIC_FIELDS, true)
			function query:onSuccess(rows)
				if not assertEqual("numeric field one", rows[1][1], 1) then return end
				if not assertEqual("numeric field two", rows[1][2], "two") then return end
				pass("numeric fields")
				done()
			end
			function query:onError(err) fail("numeric fields", err) end
			query:start()
		end
	},
	{
		name = "prepared parameters and bytea",
		run = function(done)
			runPrepared(state.db, "INSERT INTO gmsv_pg_suite (name, active, amount, big_value, exact_value, payload, maybe) VALUES ($1, $2, $3, $4, $5, $6::bytea, $7) RETURNING id, name, active, amount, big_value, exact_value, payload, maybe", function(query)
				query:setString(1, "alice")
				query:setBoolean(2, true)
				query:setNumber(3, 42)
				query:setString(4, "9223372036854775807")
				query:setString(5, "99.50")
				query:setString(6, "\\x410042")
				query:setNull(7)
			end, function(rows, query)
				local row = rows and rows[1]
				if not assertTruthy("prepared row", row) then return end
				if not assertEqual("prepared name", row.name, "alice") then return end
				if not assertEqual("prepared bool", row.active, true) then return end
				if not assertEqual("prepared int", row.amount, 42) then return end
				if not assertEqual("prepared int8 string", row.big_value, "9223372036854775807") then return end
				if not assertEqual("prepared numeric string", row.exact_value, "99.50") then return end
				if not assertEqual("bytea length", #row.payload, 3) then return end
				if not assertEqual("bytea nul", string.byte(row.payload, 2), 0) then return end
				if not assertEqual("prepared null", row.maybe, nil) then return end
				if not assertTruthy("affected rows", query:affectedRows() >= 1) then return end
				pass("prepared parameters")
				done()
			end)
		end
	},
	{
		name = "postgres question mark operator is preserved",
		run = function(done)
			runPrepared(state.db, "SELECT $1::jsonb ? 'key' AS has_key", function(query)
				query:setString(1, '{"key": true}')
			end, function(rows)
				if not assertEqual("jsonb ? operator", rows[1].has_key, true) then return end
				pass("question mark operator")
				done()
			end)
		end
	},
	{
		name = "prepared query can start multiple times",
		run = function(done)
			local query = state.db:prepare("SELECT $1::text AS value")
			local successes = 0
			function query:onSuccess(rows)
				successes = successes + 1
				if successes == 1 then
					if not assertEqual("first prepared repeat", rows[1].value, "first") then return end
					query:setString(1, "second")
					query:start()
				else
					if not assertEqual("second prepared repeat", rows[1].value, "second") then return end
					pass("prepared repeated start")
					done()
				end
			end
			function query:onError(err) fail("prepared repeat", err) end
			query:setString(1, "first")
			query:start()
		end
	},
	{
		name = "query errors include sql and getData nil",
		run = function(done)
			local query = state.db:query("SELECT definitely_missing_column")
			function query:onSuccess()
				fail("query error", "unexpected success")
			end
			function query:onError(err, sql)
				if not assertTruthy("error text", string.find(err, "definitely_missing_column", 1, true)) then return end
				if not assertEqual("error sql", sql, "SELECT definitely_missing_column") then return end
				if not assertEqual("error getData", query:getData(), nil) then return end
				pass("query error")
				done()
			end
			query:start()
		end
	},
	{
		name = "multi-statement raw query rejected",
		run = function(done)
			local query = state.db:query("SELECT 1; SELECT 2")
			function query:onSuccess()
				fail("multi statement", "unexpected success")
			end
			function query:onError(err)
				if not assertTruthy("multi statement error", string.find(err, "multiple commands", 1, true) or string.find(err, "cannot insert multiple", 1, true)) then return end
				pass("multi-statement rejected")
				done()
			end
			query:start()
		end
	},
	{
		name = "transaction success with raw and prepared queries",
		run = function(done)
			local tx = state.db:createTransaction()
			local raw = state.db:query("INSERT INTO gmsv_pg_suite (name, active) VALUES ('bob', false) RETURNING name, active")
			local prepared = state.db:prepare("INSERT INTO gmsv_pg_suite (name, active) VALUES ($1, $2) RETURNING name, active")
			prepared:setString(1, "carol")
			prepared:setBoolean(2, true)
			tx:addQuery(raw)
			tx:addQuery(prepared)
			function tx:onSuccess()
				local preparedRows = prepared:getData()
				if not assertEqual("transaction prepared name", preparedRows[1].name, "carol") then return end
				if not assertEqual("transaction prepared bool", preparedRows[1].active, true) then return end
				pass("transaction success")
				done()
			end
			function tx:onError(err) fail("transaction success", err) end
			tx:start()
		end
	},
	{
		name = "transaction rollback on error",
		run = function(done)
			runQuery(state.db, "SELECT count(*)::int AS count_before FROM gmsv_pg_suite", function(beforeRows)
				local before = beforeRows[1].count_before
				local tx = state.db:createTransaction()
				local okInsert = state.db:query("INSERT INTO gmsv_pg_suite (name, active) VALUES ('rollback', true)")
				local badQuery = state.db:query("INSERT INTO gmsv_pg_suite (missing_column) VALUES (1)")
				tx:addQuery(okInsert)
				tx:addQuery(badQuery)
				function tx:onSuccess()
					fail("rollback transaction", "unexpected success")
				end
				function tx:onError()
					runQuery(state.db, "SELECT count(*)::int AS count_after FROM gmsv_pg_suite", function(afterRows)
						if not assertEqual("rollback count", afterRows[1].count_after, before) then return end
						pass("transaction rollback")
						done()
					end)
				end
				tx:start()
			end)
		end
	},
	{
		name = "abort waiting query prevents execution",
		run = function(done)
			runQuery(state.db, "TRUNCATE gmsv_pg_suite", function()
				local blocker = state.db:query("SELECT pg_sleep(1)")
				local insert = state.db:query("INSERT INTO gmsv_pg_suite (name, active) VALUES ('aborted', true)")
				function blocker:onSuccess()
					runQuery(state.db, "SELECT count(*)::int AS count FROM gmsv_pg_suite", function(rows)
						if not assertEqual("abort prevented insert", rows[1].count, 0) then return end
						pass("abort waiting query")
						done()
					end)
				end
				function blocker:onError(err) fail("blocker", err) end
				function insert:onSuccess() fail("aborted insert", "unexpected success") end
				function insert:onAborted() pass("aborted callback") end
				blocker:start()
				insert:start()
				if not assertEqual("abort returned", insert:abort(), true) then return end
			end)
		end
	},
	{
		name = "hasMoreResults false and getNextResults throws",
		run = function(done)
			runQuery(state.db, "SELECT 1::int4 AS value", function(rows, query)
				if not assertEqual("hasMoreResults", query:hasMoreResults(), false) then return end
				assertThrows("getNextResults", function() query:getNextResults() end, "multi-statement")
				done()
			end)
		end
	}
}

nextTest()
print("[pg test] started", #state.tests, "tests")
LUA
```

Poll for results:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
for i in $(seq 1 60); do
  ./dev_eval.sh server "local s=_G.__pgTest if not s then print('missing') return end print('done', s.done, 'failed', s.failed, 'index', s.index, 'lines', #s.lines) for _,line in ipairs(s.lines) do print(line) end"
  ./dev_eval.sh server "local s=_G.__pgTest return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- Final line includes `PASS suite complete`.

## 6. Docker Restart Reconnect Test

This test verifies reconnect behavior by keeping a database object alive in GMod, restarting the Docker PostgreSQL container, then using the same database object again.

Expected behavior:

- Initial connection succeeds.
- After Docker restart, the first query on the stale socket may fail; it must not be replayed automatically.
- The module should reconnect internally for future work.
- A second query on the same database object should succeed.
- `db:onReconnect()` should fire if the reconnect succeeds.

Start a long-lived Lua-side state:

```sh
./dev_eval.sh server <<'LUA'
require("pg")
_G.__pgReconnect = { done = false, failed = false, ready = false, lines = {} }
local state = _G.__pgReconnect
local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
	state.lines[#state.lines + 1] = table.concat(parts, "\t")
	print("[pg reconnect]", state.lines[#state.lines])
end
local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
state.db = db
function db:onConnected()
	line("connected")
	state.ready = true
end
function db:onConnectionFailed(err)
	state.failed = true
	line("connect failed", err)
	state.done = true
end
function db:onReconnect()
	state.reconnected = true
	line("reconnect")
end
function db:onReconnectFailed(err)
	state.reconnectFailed = true
	line("reconnect failed", err)
end
db:connect()
LUA
```

Poll until `ready true`:

```sh
for i in $(seq 1 30); do
  ./dev_eval.sh server "local s=_G.__pgReconnect print('ready', s and s.ready, 'done', s and s.done, 'failed', s and s.failed) for _,line in ipairs((s and s.lines) or {}) do print(line) end"
  ./dev_eval.sh server "local s=_G.__pgReconnect return s and s.ready" | grep -q "return true" && break
  sleep 1
done
```

Restart PostgreSQL and wait for it:

```sh
docker restart gmsv-pg-test
for i in $(seq 1 30); do
  docker exec gmsv-pg-test pg_isready -U postgres -d gmsv_pg_test && break
  sleep 1
done
```

Trigger stale-socket handling. The first query records whether the stale socket failed or unexpectedly survived; then the second query must succeed on the same database object:

```sh
./dev_eval.sh server <<'LUA'
local state = _G.__pgReconnect
local db = state.db
local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
	state.lines[#state.lines + 1] = table.concat(parts, "\t")
	print("[pg reconnect]", state.lines[#state.lines])
end
local function runSecondQuery()
	local q2 = db:query("SELECT 2::int4 AS value")
	function q2:onSuccess(rows)
		if rows and rows[1] and rows[1].value == 2 then
			line("second query success", rows[1].value)
			state.done = true
		else
			line("second query bad rows")
			state.failed = true
			state.done = true
		end
	end
	function q2:onError(err)
		line("second query error", err)
		state.failed = true
		state.done = true
	end
	q2:start()
end
local q1 = db:query("SELECT 1::int4 AS value")
function q1:onSuccess(rows)
	line("first query success", rows and rows[1] and rows[1].value)
	runSecondQuery()
end
function q1:onError(err)
	line("first query error", err)
	runSecondQuery()
end
q1:start()
LUA
```

Poll for results:

```sh
for i in $(seq 1 30); do
  ./dev_eval.sh server "local s=_G.__pgReconnect print('done', s.done, 'failed', s.failed, 'reconnected', s.reconnected, 'reconnectFailed', s.reconnectFailed) for _,line in ipairs(s.lines) do print(line) end"
  ./dev_eval.sh server "local s=_G.__pgReconnect return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- `second query success 2`
- Usually `reconnect true`

## 7. Supplemental Edge-Case Suite

The full integration suite covers the main runtime path. Run this supplemental suite to exercise less common exported methods and edge cases.

Start the supplemental suite:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")

_G.__pgEdgeTest = {
	done = false,
	failed = false,
	index = 0,
	lines = {},
	tests = {}
}

local state = _G.__pgEdgeTest

local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[#parts + 1] = tostring(select(i, ...))
	end
	local text = table.concat(parts, "\t")
	state.lines[#state.lines + 1] = text
	print("[pg edge]", text)
end

local function fail(...)
	state.failed = true
	line("FAIL", ...)
	state.done = true
end

local function pass(...)
	line("PASS", ...)
end

local function assertEqual(name, actual, expected)
	if actual ~= expected then
		fail(name, "expected", expected, "got", actual)
		return false
	end
	return true
end

local function assertTruthy(name, value)
	if not value then
		fail(name, "expected truthy", value)
		return false
	end
	return true
end

local function assertThrows(name, fn, expectedText)
	local ok, err = pcall(fn)
	if ok then
		fail(name, "expected throw")
		return false
	end
	if expectedText and not string.find(tostring(err), expectedText, 1, true) then
		fail(name, "wrong error", err)
		return false
	end
	pass(name, "threw", err)
	return true
end

local function connectDatabase(onReady)
	local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
	function db:onConnected()
		onReady(db)
	end
	function db:onConnectionFailed(err)
		fail("connect", err)
	end
	db:connect()
	return db
end

local function runQuery(db, sql, onSuccess, onError)
	local query = db:query(sql)
	function query:onSuccess(rows)
		onSuccess(rows, query)
	end
	function query:onError(err, querySql)
		if onError then
			onError(err, querySql, query)
		else
			fail("query error", err, querySql)
		end
	end
	query:start()
	return query
end

local function nextTest()
	if state.done then
		return
	end

	state.index = state.index + 1
	local test = state.tests[state.index]
	if not test then
		state.done = true
		pass("edge suite complete")
		return
	end

	line("START", state.index, test.name)
	test.run(nextTest)
end

state.tests = {
	{
		name = "pre-connect metadata and wait errors",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			assertThrows("serverVersion before connect", function() db:serverVersion() end, "not connected")
			assertThrows("serverInfo before connect", function() db:serverInfo() end, "not connected")
			assertThrows("hostInfo before connect", function() db:hostInfo() end, "not connected")
			assertThrows("db wait before connect", function() db:wait() end, "before connect")
			done()
		end
	},
	{
		name = "connect wait and duplicate connect",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			local connected = false
			function db:onConnected()
				connected = true
			end
			function db:onConnectionFailed(err)
				fail("connect wait", err)
			end
			db:connect()
			db:wait()
			if not assertEqual("connected callback after wait", connected, true) then return end
			if not assertEqual("status after wait", db:status(), pg.DATABASE_CONNECTED) then return end
			assertThrows("duplicate connect", function() db:connect() end, "already")
			db:disconnect(true)
			pass("connect wait")
			done()
		end
	},
	{
		name = "escape and character set",
		run = function(done)
			connectDatabase(function(db)
				local escaped = db:escape("a'b\\c")
				if not assertTruthy("escape output", escaped and #escaped > 0) then return end
				local ok, err = db:setCharacterSet("UTF8")
				if not assertEqual("charset ok", ok, true) then return end
				if not assertEqual("charset err", err, "") then return end
				pass("escape and charset", escaped)
				done()
			end)
		end
	},
	{
		name = "SSL settings validation",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			db:setSSLMode(pg.SSL_MODE_PREFERRED)
			assertThrows("invalid ssl mode", function() db:setSSLMode(999) end, "Invalid SSL mode")
			assertThrows("unsupported ssl capath", function() db:setSSLSettings(nil, nil, nil, "/tmp") end, "capath")
			assertThrows("unsupported ssl cipher", function() db:setSSLSettings(nil, nil, nil, nil, "DEFAULT") end, "cipher")
			pass("ssl validation")
			done()
		end
	},
	{
		name = "setAutoReconnect is callable and prepared cache is unsupported",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			db:setAutoReconnect(false)
			db:setAutoReconnect(true)
			assertThrows("setCachePreparedStatements", function() db:setCachePreparedStatements(true) end, "not supported")
			pass("reconnect/cache setters")
			done()
		end
	},
	{
		name = "query wait, swap flag, isRunning, error accessor",
		run = function(done)
			connectDatabase(function(db)
				local query = db:query("SELECT pg_sleep(0.1), 123::int4 AS value")
				function query:onSuccess(rows)
					if not assertEqual("wait row", rows[1].value, 123) then return end
				end
				function query:onError(err) fail("query wait", err) end
				query:start()
				if not assertEqual("isRunning after start", query:isRunning(), true) then return end
				query:wait(true)
				if not assertEqual("isRunning after wait", query:isRunning(), false) then return end
				if not assertEqual("error empty", query:error(), "") then return end
				pass("query wait")
				done()
			end)
		end
	},
	{
		name = "query wait before start throws",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			local query = db:query("SELECT 1")
			assertThrows("query wait before start", function() query:wait() end, "not started")
			pass("query wait before start")
			done()
		end
	},
	{
		name = "commandStatus currently throws and oid returns numeric",
		run = function(done)
			connectDatabase(function(db)
				runQuery(db, "SELECT 1::int4 AS value", function(rows, query)
					assertThrows("commandStatus", function() query:commandStatus() end, "not available")
					if not assertEqual("oid default", query:oid(), 0) then return end
					pass("commandStatus and oid")
					done()
				end)
			end)
		end
	},
	{
		name = "invalid query option throws",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			local query = db:query("SELECT 1")
			assertThrows("invalid option", function() query:setOption(999, true) end, "Invalid")
			pass("invalid query option")
			done()
		end
	},
	{
		name = "prepared parameter validation and clearParameters",
		run = function(done)
			connectDatabase(function(db)
				local query = db:prepare("SELECT $1::text AS value")
				assertThrows("setString index 0", function() query:setString(0, "bad") end, "greater than 0")
				query:setString(1, "before-clear")
				query:clearParameters()
				query:setString(1, "after-clear")
				function query:onSuccess(rows)
					if not assertEqual("clearParameters value", rows[1].value, "after-clear") then return end
					pass("prepared validation")
					done()
				end
				function query:onError(err) fail("prepared validation", err) end
				query:start()
			end)
		end
	},
	{
		name = "putNewParameters is unsupported until multi-results exist",
		run = function(done)
			connectDatabase(function(db)
				local query = db:prepare("SELECT $1::text AS value")
				query:setString(1, "first")
				assertThrows("putNewParameters", function() query:putNewParameters() end, "not supported")
				function query:onSuccess(rows)
					if not assertEqual("prepared still usable", rows[1].value, "first") then return end
					pass("putNewParameters unsupported")
					done()
				end
				function query:onError(err) fail("putNewParameters", err) end
				query:start()
			end)
		end
	},
	{
		name = "transaction getQueries and clearQueries",
		run = function(done)
			connectDatabase(function(db)
				local tx = db:createTransaction()
				local q = db:query("SELECT 1::int4 AS value")
				tx:addQuery(q)
				if not assertEqual("getQueries count", #tx:getQueries(), 1) then return end
				tx:clearQueries()
				if not assertEqual("clearQueries count", #tx:getQueries(), 0) then return end
				pass("transaction get/clear")
				done()
			end)
		end
	},
	{
		name = "abortAllQueries prevents queued inserts",
		run = function(done)
			local lockKey = 987654
			connectDatabase(function(db)
				connectDatabase(function(lockDb)
					runQuery(db, "DROP TABLE IF EXISTS gmsv_pg_abort_all", function()
						runQuery(db, "CREATE TABLE gmsv_pg_abort_all (value int4)", function()
							runQuery(lockDb, "SELECT pg_advisory_lock(" .. lockKey .. ")", function()
								local blocker = db:query("SELECT pg_advisory_lock(" .. lockKey .. ")")
								function blocker:onSuccess()
									runQuery(db, "SELECT pg_advisory_unlock(" .. lockKey .. ")", function()
										runQuery(db, "SELECT count(*)::int AS count FROM gmsv_pg_abort_all", function(rows)
											if not assertEqual("abortAll count", rows[1].count, 0) then return end
											pass("abortAllQueries")
											done()
										end)
									end)
								end
								function blocker:onError(err) fail("abortAll blocker", err) end
								blocker:start()

								timer.Simple(0.2, function()
									local q1 = db:query("INSERT INTO gmsv_pg_abort_all VALUES (1)")
									local q2 = db:query("INSERT INTO gmsv_pg_abort_all VALUES (2)")
									q1:start()
									q2:start()
									local aborted = db:abortAllQueries()
									if not assertTruthy("abortAll returned count", aborted >= 2) then return end
									runQuery(lockDb, "SELECT pg_advisory_unlock(" .. lockKey .. ")", function() end)
								end)
							end)
						end)
					end)
				end)
			end)
		end
	},
	{
		name = "running query abort cancels PostgreSQL work",
		run = function(done)
			connectDatabase(function(db)
				local startedAt = SysTime()
				local query = db:query("SELECT pg_sleep(5)")
				function query:onSuccess()
					fail("running abort", "unexpected success")
				end
				function query:onError(err)
					fail("running abort", "unexpected error", err)
				end
				function query:onAborted()
					local elapsed = SysTime() - startedAt
					if not assertTruthy("running abort elapsed", elapsed < 2.0) then return end
					pass("running query abort", elapsed)
					done()
				end
				query:start()
				timer.Simple(0.2, function()
					if not assertEqual("running abort returned", query:abort(), true) then return end
				end)
			end)
		end
	},
	{
		name = "disconnect callback",
		run = function(done)
			local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
			local disconnected = false
			function db:onConnected()
				db:disconnect(true)
			end
			function db:onDisconnected()
				disconnected = true
				pass("disconnect callback")
				done()
			end
			function db:onConnectionFailed(err)
				fail("disconnect connect", err)
			end
			db:connect()
		end
	}
}

nextTest()
print("[pg edge] started", #state.tests, "tests")
LUA
```

Poll for results:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
for i in $(seq 1 60); do
  ./dev_eval.sh server "local s=_G.__pgEdgeTest if not s then print('missing') return end print('done', s.done, 'failed', s.failed, 'index', s.index, 'lines', #s.lines) for _,line in ipairs(s.lines) do print(line) end"
  ./dev_eval.sh server "local s=_G.__pgEdgeTest return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- Final line includes `PASS edge suite complete`.

## 8. High-Concurrency Suite

This suite stresses queueing, callback delivery, multiple database objects, error isolation, transaction batching, and abort behavior under load. It is intentionally heavier than the normal integration suite but should still complete quickly on a local machine.

Start the high-concurrency suite:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")

_G.__pgConcurrencyTest = {
	done = false,
	failed = false,
	index = 0,
	lines = {},
	tests = {}
}

local state = _G.__pgConcurrencyTest

local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[#parts + 1] = tostring(select(i, ...))
	end
	local text = table.concat(parts, "\t")
	state.lines[#state.lines + 1] = text
	print("[pg concurrency]", text)
end

local function fail(...)
	state.failed = true
	line("FAIL", ...)
	state.done = true
end

local function pass(...)
	line("PASS", ...)
end

local function assertEqual(name, actual, expected)
	if actual ~= expected then
		fail(name, "expected", expected, "got", actual)
		return false
	end
	return true
end

local function assertTruthy(name, value)
	if not value then
		fail(name, "expected truthy", value)
		return false
	end
	return true
end

local function connectDatabase(onReady)
	local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
	function db:onConnected()
		onReady(db)
	end
	function db:onConnectionFailed(err)
		fail("connect", err)
	end
	db:connect()
	return db
end

local function runQuery(db, sql, onSuccess, onError)
	local query = db:query(sql)
	function query:onSuccess(rows)
		onSuccess(rows, query)
	end
	function query:onError(err, querySql)
		if onError then
			onError(err, querySql, query)
		else
			fail("query error", err, querySql)
		end
	end
	query:start()
	return query
end

local function nextTest()
	if state.done then
		return
	end

	state.index = state.index + 1
	local test = state.tests[state.index]
	if not test then
		state.done = true
		pass("concurrency suite complete")
		return
	end

	line("START", state.index, test.name)
	test.run(nextTest)
end

state.tests = {
	{
		name = "queued select burst on one database",
		run = function(done)
			connectDatabase(function(db)
				local total = 200
				local finished = 0
				local sum = 0
				for i = 1, total do
					local expected = i
					local query = db:query("SELECT " .. expected .. "::int4 AS value")
					query.onSuccess = function(_, rows)
						finished = finished + 1
						sum = sum + rows[1].value
						if finished == total then
							if not assertEqual("select burst sum", sum, total * (total + 1) / 2) then return end
							pass("queued select burst", total)
							done()
						end
					end
					query.onError = function(_, err) fail("select burst", expected, err) end
					query:start()
				end
			end)
		end
	},
	{
		name = "prepared insert burst on one database",
		run = function(done)
			connectDatabase(function(db)
				runQuery(db, "DROP TABLE IF EXISTS gmsv_pg_concurrency", function()
					runQuery(db, "CREATE TABLE gmsv_pg_concurrency (id serial PRIMARY KEY, bucket text NOT NULL, value int4 NOT NULL)", function()
						local total = 150
						local finished = 0
						for i = 1, total do
							local expected = i
							local query = db:prepare("INSERT INTO gmsv_pg_concurrency (bucket, value) VALUES ($1, $2) RETURNING value")
							query:setString(1, "prepared-burst")
							query:setNumber(2, expected)
							query.onSuccess = function(_, rows)
								finished = finished + 1
								if finished == total then
									runQuery(db, "SELECT count(*)::int AS count, sum(value)::int AS sum FROM gmsv_pg_concurrency WHERE bucket = 'prepared-burst'", function(rows2)
										if not assertEqual("prepared burst count", rows2[1].count, total) then return end
										if not assertEqual("prepared burst sum", rows2[1].sum, total * (total + 1) / 2) then return end
										pass("prepared insert burst", total)
										done()
									end)
								end
							end
							query.onError = function(_, err) fail("prepared burst", expected, err) end
							query:start()
						end
					end)
				end)
			end)
		end
	},
	{
		name = "mixed success and error burst does not poison queue",
		run = function(done)
			connectDatabase(function(db)
				local total = 80
				local successes = 0
				local errors = 0
				local finished = 0
				local function maybeDone()
					if finished ~= total then return end
					if not assertEqual("mixed success count", successes, 40) then return end
					if not assertEqual("mixed error count", errors, 40) then return end
					runQuery(db, "SELECT 123::int4 AS after_error", function(rows)
						if not assertEqual("queue survived errors", rows[1].after_error, 123) then return end
						pass("mixed success/error burst")
						done()
					end)
				end
				for i = 1, total do
					local sql = (i % 2 == 0) and ("SELECT " .. i .. "::int4 AS value") or "SELECT definitely_missing_column"
					local query = db:query(sql)
					query.onSuccess = function()
						successes = successes + 1
						finished = finished + 1
						maybeDone()
					end
					query.onError = function()
						errors = errors + 1
						finished = finished + 1
						maybeDone()
					end
					query:start()
				end
			end)
		end
	},
	{
		name = "multiple databases run independently",
		run = function(done)
			local dbCount = 6
			local perDb = 40
			local connected = 0
			local completedDbs = 0
			local function startDbWork(db, dbIndex)
				local finished = 0
				local sum = 0
				for i = 1, perDb do
					local expected = dbIndex * 1000 + i
					local queryIndex = i
					local query = db:query("SELECT " .. expected .. "::int4 AS value")
					query.onSuccess = function(_, rows)
						finished = finished + 1
						sum = sum + rows[1].value
						if finished == perDb then
							local expectedSum = perDb * dbIndex * 1000 + perDb * (perDb + 1) / 2
							if not assertEqual("multi-db sum " .. dbIndex, sum, expectedSum) then return end
							completedDbs = completedDbs + 1
							if completedDbs == dbCount then
								pass("multiple databases", dbCount, perDb)
								done()
							end
						end
					end
					query.onError = function(_, err) fail("multi-db", dbIndex, queryIndex, err) end
					query:start()
				end
			end
			for dbIndex = 1, dbCount do
				connectDatabase(function(db)
					connected = connected + 1
					startDbWork(db, dbIndex)
				end)
			end
		end
	},
	{
		name = "transaction burst",
		run = function(done)
			connectDatabase(function(db)
				runQuery(db, "TRUNCATE gmsv_pg_concurrency", function()
					local total = 40
					local finished = 0
					for i = 1, total do
						local expected = i
						local tx = db:createTransaction()
						local q1 = db:prepare("INSERT INTO gmsv_pg_concurrency (bucket, value) VALUES ($1, $2)")
						local q2 = db:prepare("INSERT INTO gmsv_pg_concurrency (bucket, value) VALUES ($1, $2)")
						q1:setString(1, "tx-burst")
						q1:setNumber(2, expected)
						q2:setString(1, "tx-burst")
						q2:setNumber(2, expected + 1000)
						tx:addQuery(q1)
						tx:addQuery(q2)
						tx.onSuccess = function()
							finished = finished + 1
							if finished == total then
								runQuery(db, "SELECT count(*)::int AS count FROM gmsv_pg_concurrency WHERE bucket = 'tx-burst'", function(rows)
									if not assertEqual("transaction burst count", rows[1].count, total * 2) then return end
									pass("transaction burst", total)
									done()
								end)
							end
						end
						tx.onError = function(_, err) fail("transaction burst", expected, err) end
						tx:start()
					end
				end)
			end)
		end
	},
	{
		name = "abort under queued load",
		run = function(done)
			local lockKey = 456789
			connectDatabase(function(db)
				connectDatabase(function(lockDb)
					runQuery(db, "TRUNCATE gmsv_pg_concurrency", function()
							runQuery(lockDb, "SELECT pg_advisory_lock(" .. lockKey .. ")", function()
							local blocker = db:query("SELECT pg_advisory_lock(" .. lockKey .. ")")
							blocker.onSuccess = function()
								runQuery(db, "SELECT count(*)::int AS count FROM gmsv_pg_concurrency WHERE bucket = 'abort-load'", function(rows)
									if not assertEqual("abort load count", rows[1].count, 0) then return end
									pass("abort under load")
									done()
								end)
							end
							blocker.onError = function(_, err) fail("abort load blocker", err) end
							blocker:start()

							timer.Simple(0.2, function()
								local total = 100
								for i = 1, total do
									local expected = i
									local query = db:query("INSERT INTO gmsv_pg_concurrency (bucket, value) VALUES ('abort-load', " .. expected .. ")")
									query.onSuccess = function() fail("abort load", "unexpected insert", expected) end
									query:start()
								end
								local aborted = db:abortAllQueries()
								if not assertTruthy("abort load count returned", aborted >= 90) then return end
								runQuery(lockDb, "SELECT pg_advisory_unlock(" .. lockKey .. ")", function() end)
							end)
						end)
					end)
				end)
			end)
		end
	}
}

nextTest()
print("[pg concurrency] started", #state.tests, "tests")
LUA
```

Poll for results:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
for i in $(seq 1 120); do
  ./dev_eval.sh server "local s=_G.__pgConcurrencyTest if not s then print('missing') return end print('done', s.done, 'failed', s.failed, 'index', s.index, 'lines', #s.lines) for _,line in ipairs(s.lines) do print(line) end"
  ./dev_eval.sh server "local s=_G.__pgConcurrencyTest return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- Final line includes `PASS concurrency suite complete`.

## 9. Test Coverage Checklist

## 9. Parallel Connection Stress Suite

Run this after the normal high-concurrency suite when you want a heavier local stress pass. This suite is focused on real concurrency: multiple `Database` objects, each with its own connection and worker. A single `Database` still intentionally serializes its queue, matching the MySQLOO-style runtime model and PostgreSQL connection semantics.

Start the stress suite:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")

_G.__pgStressTest = {
	done = false,
	failed = false,
	index = 0,
	lines = {},
	tests = {}
}

local state = _G.__pgStressTest

local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[#parts + 1] = tostring(select(i, ...))
	end
	local text = table.concat(parts, "\t")
	state.lines[#state.lines + 1] = text
	print("[pg stress]", text)
end

local function fail(...)
	state.failed = true
	line("FAIL", ...)
	state.done = true
end

local function pass(...)
	line("PASS", ...)
end

local function assertEqual(name, actual, expected)
	if actual ~= expected then
		fail(name, "expected", expected, "got", actual)
		return false
	end
	return true
end

local function assertTruthy(name, value)
	if not value then
		fail(name, "expected truthy", value)
		return false
	end
	return true
end

local function connectDatabase(onReady)
	local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
	function db:onConnected()
		onReady(db)
	end
	function db:onConnectionFailed(err)
		fail("connect", err)
	end
	db:connect()
	return db
end

local function runQuery(db, sql, onSuccess, onError)
	local query = db:query(sql)
	query.onSuccess = function(_, rows)
		onSuccess(rows, query)
	end
	query.onError = function(_, err, querySql)
		if onError then
			onError(err, querySql, query)
		else
			fail("query error", err, querySql)
		end
	end
	query:start()
	return query
end

local function nextTest()
	if state.done then
		return
	end

	state.index = state.index + 1
	local test = state.tests[state.index]
	if not test then
		state.done = true
		pass("parallel stress suite complete")
		return
	end

	line("START", state.index, test.name)
	test.run(nextTest)
end

state.tests = {
	{
		name = "single-connection queue soak",
		run = function(done)
			connectDatabase(function(db)
				local total = 500
				local finished = 0
				local sum = 0
				for i = 1, total do
					local expected = i
					local query = db:query("SELECT " .. expected .. "::int4 AS value")
					query.onSuccess = function(_, rows)
						finished = finished + 1
						sum = sum + rows[1].value
						if finished == total then
							if not assertEqual("single connection soak sum", sum, total * (total + 1) / 2) then return end
							pass("single-connection queue soak", total)
							done()
						end
					end
					query.onError = function(_, err) fail("single connection soak", expected, err) end
					query:start()
				end
			end)
		end
	},
	{
		name = "prepared insert stress on one database",
		run = function(done)
			connectDatabase(function(db)
				runQuery(db, "DROP TABLE IF EXISTS gmsv_pg_stress", function()
					runQuery(db, "CREATE TABLE gmsv_pg_stress (id serial PRIMARY KEY, bucket text NOT NULL, value int4 NOT NULL)", function()
						local total = 500
						local finished = 0
						for i = 1, total do
							local expected = i
							local query = db:prepare("INSERT INTO gmsv_pg_stress (bucket, value) VALUES ($1, $2) RETURNING value")
							query:setString(1, "prepared-stress")
							query:setNumber(2, expected)
							query.onSuccess = function()
								finished = finished + 1
								if finished == total then
									runQuery(db, "SELECT count(*)::int AS count, sum(value)::int AS sum FROM gmsv_pg_stress WHERE bucket = 'prepared-stress'", function(rows)
										if not assertEqual("prepared stress count", rows[1].count, total) then return end
										if not assertEqual("prepared stress sum", rows[1].sum, total * (total + 1) / 2) then return end
										pass("prepared insert stress", total)
										done()
									end)
								end
							end
							query.onError = function(_, err) fail("prepared stress", expected, err) end
							query:start()
						end
					end)
				end)
			end)
		end
	},
	{
		name = "mixed success and error stress",
		run = function(done)
			connectDatabase(function(db)
				local total = 800
				local successes = 0
				local errors = 0
				local finished = 0
				local function maybeDone()
					if finished ~= total then return end
					if not assertEqual("mixed stress success count", successes, 400) then return end
					if not assertEqual("mixed stress error count", errors, 400) then return end
					runQuery(db, "SELECT 456::int4 AS after_error", function(rows)
						if not assertEqual("mixed stress queue survived errors", rows[1].after_error, 456) then return end
						pass("mixed success/error stress")
						done()
					end)
				end
				for i = 1, total do
					local sql = (i % 2 == 0) and ("SELECT " .. i .. "::int4 AS value") or "SELECT definitely_missing_column"
					local query = db:query(sql)
					query.onSuccess = function()
						successes = successes + 1
						finished = finished + 1
						maybeDone()
					end
					query.onError = function()
						errors = errors + 1
						finished = finished + 1
						maybeDone()
					end
					query:start()
				end
			end)
		end
	},
	{
		name = "20 database connections overlap blocking work",
		run = function(done)
			local dbCount = 20
			local sleepSeconds = 0.2
			local connected = 0
			local finished = 0
			local databases = {}
			local startedAt = nil
			local function startWork(db, dbIndex)
				local query = db:query("SELECT pg_sleep(" .. sleepSeconds .. "), " .. dbIndex .. "::int4 AS db_index")
				query.onSuccess = function(_, rows)
					finished = finished + 1
					if finished == dbCount then
						local elapsed = SysTime() - startedAt
						if not assertTruthy("20 connection overlap elapsed", elapsed < 3.0) then return end
						pass("20 database parallel sleep", dbCount, "elapsed", elapsed)
						done()
					end
				end
				query.onError = function(_, err) fail("20 db sleep", dbIndex, err) end
				query:start()
			end
			for dbIndex = 1, dbCount do
				local index = dbIndex
				connectDatabase(function(db)
					databases[index] = db
					connected = connected + 1
					if connected == dbCount then
						startedAt = SysTime()
						for startIndex, startDb in ipairs(databases) do
							startWork(startDb, startIndex)
						end
					end
				end)
			end
		end
	},
	{
		name = "20 database connections run independently",
		run = function(done)
			local dbCount = 20
			local completedDbs = 0
			local function startDbWork(db, dbIndex)
				local query = db:query("SELECT " .. dbIndex .. "::int4 AS db_index, sum(value)::int AS sum FROM generate_series(1, 100) AS value")
				query.onSuccess = function(_, rows)
					if not assertEqual("20-db index " .. dbIndex, rows[1].db_index, dbIndex) then return end
					if not assertEqual("20-db aggregate " .. dbIndex, rows[1].sum, 5050) then return end
					completedDbs = completedDbs + 1
					if completedDbs == dbCount then
						pass("20 database independent work", dbCount)
						done()
					end
				end
				query.onError = function(_, err) fail("20-db work", dbIndex, err) end
				query:start()
			end
			for dbIndex = 1, dbCount do
				connectDatabase(function(db)
					startDbWork(db, dbIndex)
				end)
			end
		end
	},
	{
		name = "transaction stress",
		run = function(done)
			connectDatabase(function(db)
				runQuery(db, "TRUNCATE gmsv_pg_stress", function()
					local total = 100
					local finished = 0
					for i = 1, total do
						local expected = i
						local tx = db:createTransaction()
						local q1 = db:prepare("INSERT INTO gmsv_pg_stress (bucket, value) VALUES ($1, $2)")
						local q2 = db:prepare("INSERT INTO gmsv_pg_stress (bucket, value) VALUES ($1, $2)")
						q1:setString(1, "tx-stress")
						q1:setNumber(2, expected)
						q2:setString(1, "tx-stress")
						q2:setNumber(2, expected + 10000)
						tx:addQuery(q1)
						tx:addQuery(q2)
						tx.onSuccess = function()
							finished = finished + 1
							if finished == total then
								runQuery(db, "SELECT count(*)::int AS count FROM gmsv_pg_stress WHERE bucket = 'tx-stress'", function(rows)
									if not assertEqual("transaction stress count", rows[1].count, total * 2) then return end
									pass("transaction stress", total)
									done()
								end)
							end
						end
						tx.onError = function(_, err) fail("transaction stress", expected, err) end
						tx:start()
					end
				end)
			end)
		end
	},
	{
		name = "abort under queued stress",
		run = function(done)
			local lockKey = 456790
			connectDatabase(function(db)
				connectDatabase(function(lockDb)
					runQuery(db, "TRUNCATE gmsv_pg_stress", function()
						runQuery(lockDb, "SELECT pg_advisory_lock(" .. lockKey .. ")", function()
							local blocker = db:query("SELECT pg_advisory_lock(" .. lockKey .. ")")
							blocker.onSuccess = function()
								runQuery(db, "SELECT count(*)::int AS count FROM gmsv_pg_stress WHERE bucket = 'abort-stress'", function(rows)
									if not assertEqual("10x abort count", rows[1].count, 0) then return end
									pass("10x abort under load")
									done()
								end)
							end
							blocker.onError = function(_, err) fail("10x abort blocker", err) end
							blocker:start()

							timer.Simple(0.2, function()
								local total = 300
								for i = 1, total do
									local expected = i
									local query = db:query("INSERT INTO gmsv_pg_stress (bucket, value) VALUES ('abort-stress', " .. expected .. ")")
									query.onSuccess = function() fail("abort stress", "unexpected insert", expected) end
									query:start()
								end
								local aborted = db:abortAllQueries()
								if not assertTruthy("abort stress returned count", aborted >= 250) then return end
								runQuery(lockDb, "SELECT pg_advisory_unlock(" .. lockKey .. ")", function() end)
							end)
						end)
					end)
				end)
			end)
		end
	}
}

nextTest()
print("[pg stress] started", #state.tests, "tests")
LUA
```

Poll for results:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
for i in $(seq 1 300); do
  ./dev_eval.sh server "local s=_G.__pgStressTest if not s then print('missing') return end print('done', s.done, 'failed', s.failed, 'index', s.index, 'lines', #s.lines) for _,line in ipairs(s.lines) do print(line) end"
  ./dev_eval.sh server "local s=_G.__pgStressTest return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- Final line includes `PASS parallel stress suite complete`.

## 10. Test Coverage Checklist

## 10. Big Callback Insert Stress Suite

This suite is intentionally heavy. It creates many Lua callback deliveries and many PostgreSQL writes:

- 50 database objects/connections.
- 500 insert statements per connection.
- 10 rows inserted per statement.
- 25,000 query callbacks.
- 250,000 inserted rows.

Run this only after the normal and parallel stress suites pass.

Before starting, clear old test globals so previous database objects can be garbage collected and disconnect their workers:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server "_G.__pgTest=nil _G.__pgEdgeTest=nil _G.__pgReconnect=nil _G.__pgConcurrencyTest=nil _G.__pgStressTest=nil _G.__pgBigInsertTest=nil collectgarbage() collectgarbage() print('cleared pg test globals')"
```

Start the suite:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")

_G.__pgBigInsertTest = {
	done = false,
	failed = false,
	lines = {},
	startedAt = SysTime()
}

local state = _G.__pgBigInsertTest

local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[#parts + 1] = tostring(select(i, ...))
	end
	local text = table.concat(parts, "\t")
	state.lines[#state.lines + 1] = text
	print("[pg big insert]", text)
end

local function fail(...)
	state.failed = true
	line("FAIL", ...)
	state.done = true
end

local function pass(...)
	line("PASS", ...)
end

local function connectDatabase(onReady)
	local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
	function db:onConnected()
		onReady(db)
	end
	function db:onConnectionFailed(err)
		fail("connect", err)
	end
	db:connect()
	return db
end

local function runQuery(db, sql, onSuccess, onError)
	local query = db:query(sql)
	query.onSuccess = function(_, rows)
		onSuccess(rows, query)
	end
	query.onError = function(_, err, querySql)
		if onError then
			onError(err, querySql, query)
		else
			fail("query error", err, querySql)
		end
	end
	query:start()
	return query
end

local connectionCount = 50
local insertsPerConnection = 500
local rowsPerInsert = 10
local expectedCallbacks = connectionCount * insertsPerConnection
local expectedRows = expectedCallbacks * rowsPerInsert

local setupDb = connectDatabase(function(setupDb)
	runQuery(setupDb, "DROP TABLE IF EXISTS gmsv_pg_big_insert", function()
		runQuery(setupDb, "CREATE UNLOGGED TABLE gmsv_pg_big_insert (connection_id int4 NOT NULL, insert_id int4 NOT NULL, row_value int4 NOT NULL)", function()
			pass("setup")
			local connected = 0
			local callbacks = 0
			local connectionsDone = 0
			local connections = {}
			state.connections = connections

			local function maybeVerify()
				if connectionsDone ~= connectionCount then return end
				runQuery(setupDb, "SELECT count(*)::int AS count, count(DISTINCT connection_id)::int AS connections, sum(row_value)::int AS row_sum FROM gmsv_pg_big_insert", function(rows)
					local row = rows[1]
					if row.count ~= expectedRows then return fail("row count", "expected", expectedRows, "got", row.count) end
					if row.connections ~= connectionCount then return fail("connection count", "expected", connectionCount, "got", row.connections) end
					local expectedRowSum = expectedCallbacks * (rowsPerInsert * (rowsPerInsert + 1) / 2)
					if row.row_sum ~= expectedRowSum then return fail("row sum", "expected", expectedRowSum, "got", row.row_sum) end
					local elapsed = SysTime() - state.startedAt
					pass("big insert callbacks", callbacks)
					pass("big insert rows", row.count)
					pass("big insert elapsed", elapsed)
					state.done = true
				end)
			end

			local function startConnectionWork(db, connectionId)
				local finishedForConnection = 0
				for insertId = 1, insertsPerConnection do
					local localInsertId = insertId
					local sql = "INSERT INTO gmsv_pg_big_insert (connection_id, insert_id, row_value) SELECT " .. connectionId .. ", " .. localInsertId .. ", generate_series(1, " .. rowsPerInsert .. ")"
					local query = db:query(sql)
					query.onSuccess = function()
						callbacks = callbacks + 1
						finishedForConnection = finishedForConnection + 1
						if finishedForConnection == insertsPerConnection then
							connectionsDone = connectionsDone + 1
							if connectionsDone % 5 == 0 or connectionsDone == connectionCount then
								line("connections done", connectionsDone, "callbacks", callbacks)
							end
							maybeVerify()
						end
					end
					query.onError = function(_, err)
						fail("insert error", connectionId, localInsertId, err)
					end
					query:start()
				end
			end

			for connectionId = 1, connectionCount do
				local localConnectionId = connectionId
				connections[localConnectionId] = connectDatabase(function(db)
					connected = connected + 1
					if connected == connectionCount then
						pass("connections ready", connectionCount)
						for startConnectionId, startDb in ipairs(connections) do
							startConnectionWork(startDb, startConnectionId)
						end
					end
				end)
			end
		end)
	end)
end)

print("[pg big insert] started", connectionCount, insertsPerConnection, rowsPerInsert)
LUA
```

Poll for results:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
for i in $(seq 1 900); do
  ./dev_eval.sh server "local s=_G.__pgBigInsertTest if not s then print('missing') return end print('done', s.done, 'failed', s.failed, 'lines', #s.lines, 'last', s.lines[#s.lines])"
  ./dev_eval.sh server "local s=_G.__pgBigInsertTest return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- `PASS big insert callbacks 25000`
- `PASS big insert rows 250000`

## 11. Test Coverage Checklist

## 11. Connection GC Lifecycle Exhaustion Test

This test catches leaked PostgreSQL sessions when Lua drops database objects. It creates 1000 database connections in batches of 25, waits for each batch to connect, drops Lua references, forces garbage collection, and verifies the next batch can still connect.

If database destruction does not close PostgreSQL sessions promptly, this test will eventually fail with PostgreSQL errors such as `sorry, too many clients already`.

Start the lifecycle test:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
./dev_eval.sh server <<'LUA'
require("pg")

_G.__pgLifecycleTest = {
	done = false,
	failed = false,
	lines = {},
	batch = 0,
	created = 0
}

local state = _G.__pgLifecycleTest

local function line(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[#parts + 1] = tostring(select(i, ...))
	end
	local text = table.concat(parts, "\t")
	state.lines[#state.lines + 1] = text
	print("[pg lifecycle]", text)
end

local function fail(...)
	state.failed = true
	line("FAIL", ...)
	state.done = true
end

local function pass(...)
	line("PASS", ...)
end

local totalConnections = 1000
local batchSize = 25
local activeBatch = nil

local function forceGc()
	collectgarbage()
	collectgarbage()
end

local function startBatch()
	if state.done then return end
	if state.created >= totalConnections then
		forceGc()
		pass("lifecycle complete", state.created)
		state.done = true
		return
	end

	state.batch = state.batch + 1
	activeBatch = {}
	state.activeBatch = activeBatch
	local connected = 0
	local target = math.min(batchSize, totalConnections - state.created)
	line("batch start", state.batch, "target", target, "created", state.created)

	for i = 1, target do
		local db = pg.connect("127.0.0.1", "postgres", "postgres", "gmsv_pg_test", 55432)
		activeBatch[i] = db
		function db:onConnected()
			connected = connected + 1
			state.created = state.created + 1
			if connected == target then
				line("batch connected", state.batch, "total", state.created)
				state.activeBatch = nil
				activeBatch = nil
				forceGc()
				timer.Simple(0.1, startBatch)
			end
		end
		function db:onConnectionFailed(err)
			fail("connect failed", "batch", state.batch, "created", state.created, err)
		end
		db:connect()
	end
end

startBatch()
print("[pg lifecycle] started", totalConnections, batchSize)
LUA
```

Poll for results:

```sh
cd /home/maurits/.steam/steam/steamapps/common/GarrysMod/garrysmod
for i in $(seq 1 300); do
  ./dev_eval.sh server "local s=_G.__pgLifecycleTest if not s then print('missing') return end print('done', s.done, 'failed', s.failed, 'batch', s.batch, 'created', s.created, 'lines', #s.lines, 'last', s.lines[#s.lines])"
  ./dev_eval.sh server "local s=_G.__pgLifecycleTest return s and s.done" | grep -q "return true" && break
  sleep 1
done
```

Expected:

- `done true`
- `failed false`
- `PASS lifecycle complete 1000`

## 12. Test Coverage Checklist

The executable suite above covers:

- Module load and constants.
- `pg.connect` and `pg.new_connection` existence.
- Positional, DSN, and option-table constructors.
- Pre-connect metadata errors.
- `db:wait()` success and pre-connect error behavior.
- Connection failure callback.
- Successful connection callback.
- Disconnect callback.
- Metadata methods after connect.
- `ping`.
- `escape`.
- `setCharacterSet`.
- `setSSLMode` and `setSSLSettings` validation.
- `setAutoReconnect` callability and `setCachePreparedStatements` unsupported behavior.
- Raw queries.
- `query:wait`, `query:isRunning`, and `query:error`.
- Prepared queries with `$1` placeholders.
- `setNumber`, `setString`, `setBoolean`, `setNull`.
- `clearParameters` and `putNewParameters` unsupported behavior.
- Prepared parameter index validation.
- Repeated prepared query starts.
- PostgreSQL `?` JSONB operator preservation.
- Result conversion for text, int4, int8, numeric, boolean, null, and bytea.
- `onData` callbacks.
- `OPTION_NUMERIC_FIELDS`.
- Invalid query option errors.
- Query error callback and SQL argument.
- Multi-statement rejection.
- Transaction success with raw and prepared queries.
- `transaction:getQueries` and `transaction:clearQueries`.
- Transaction rollback on child query error.
- Waiting-query abort behavior.
- `abortAllQueries` behavior.
- Running query cancellation through `query:abort()`.
- `lastInsert()` throwing with `RETURNING` guidance.
- `setMultiStatements(true)` throwing until multi-result chains are implemented.
- `setReadTimeout` / `setWriteTimeout` throwing honestly.
- `hasMoreResults()` / `getNextResults()` behavior.
- `commandStatus()` current unsupported behavior.
- `oid()` default behavior.
- Queued query bursts on one database.
- Prepared insert bursts.
- Mixed success/error queue isolation.
- Multiple database objects running work independently.
- 20 database connections overlapping blocking work.
- 50 database connections inserting concurrently with 25,000 callbacks and 250,000 rows.
- Database Lua GC lifecycle across 1,000 connections in batches of 25.
- Transaction bursts.
- Abort behavior under queued load.

Still needs dedicated future tests when features are implemented:

- `COPY FROM` / `COPY TO`.
- `LISTEN` / `NOTIFY`.
- Savepoints.
- Notice/warning callbacks.
- Direct command status once supported.
- Rich option validation for all libpq connection options.
