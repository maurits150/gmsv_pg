1. Locate the files for your OS (windows or linux).
2. Put these files where the server process can load shared libraries. For local Garry's Mod this is usually the game root or `bin/` folder; for srcds it is the folder where the executable is:

On Windows:
`.\SERVER_DIR\srcds.exe`

Windows dependency loading is unverified. The bundled PostgreSQL DLLs are kept together under `runtime_depends/windows/bin/`; copy them together into a directory the Windows loader searches. If the module fails to load, copy those DLLs beside `garrysmod/lua/bin/gmsv_pg_win32.dll` and verify the remaining imports with Dependencies/Dependency Walker. The currently bundled `libpq.dll` imports the VC++ 2010-era `MSVCR100.dll`, so the matching x86 redistributable may be required.

On any Linux:
`./SERVER_DIR/srcds_linux`

Yes, if a runtime dependency folder contains a `bin` folder, that folder is supposed to go on top of the server's `bin` folder.

On Linux, `libpq.so.5` must match the host's available SSL/Kerberos/LDAP/SASL and other transitive libpq runtime libraries. If the module fails to load with a missing runtime library such as `libssl.so.1.1`, inspect `ldd runtime_depends/linux/libpq.so.5`, install the 32-bit libpq package, and refresh the bundled runtime dependency from the host:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install libpq-dev:i386
cp /lib/i386-linux-gnu/libpq.so.5 runtime_depends/linux/libpq.so.5
```

**In case it doesn't work on Linux:**
```sh
# add postgresql apt repos beforehand!
sudo apt-get install libpq-dev:i386
```
