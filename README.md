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
