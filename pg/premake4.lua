local target_os = os.get()
local suffixes = {
  linux   = "_linux",
  macosx  = "_macosx",
  windows = "_win32",
}
local libpq_include = target_os == "windows" and "include/libpq" or "/usr/include/postgresql"

solution "pg"
  location "./project"
  configurations { "Release" }
  flags { "NoPCH", "NoImportLib", "Symbols", "NoEditAndContinue", "EnableSSE" }
  if target_os == "windows" then
    platforms { "x32" }
  end

  if target_os ~= "windows" then
    buildoptions { "-m32", "-fPIC" }
    linkoptions { "-m32", "-static-libstdc++", "-static-libgcc" }
  end

  configuration "Release"
    defines { "NDEBUG" }
    flags { "Optimize", "FloatFast" }

  configuration {}

project "pg"
  kind "SharedLib"
  language "C++"
  location "./project"
  targetdir "./bin"
  targetprefix ""
  targetname("gmsv_pg"..(suffixes[target_os] or "_"..target_os))
  targetextension ".dll"

  defines { "GMMODULE" }
  libdirs { "lib/"..target_os }
  includedirs {
    libpq_include,
    "../vendor/gmod-lua",
    "../include",
    "../vendor/gmod-module-base/include",
    "../vendor/variant/include/mpark"
  }

  if target_os ~= "windows" then
    buildoptions { "-std=c++11" }
  end

  files {
    "src/lua/**.cpp",
    "src/lua/**.h",
    "src/postgres/**.cpp",
    "src/postgres/**.h",
    "src/BlockingQueue.h"
  }

  if target_os == "windows" then
    links { "ws2_32", "wsock32", "secur32", "wldap32", "advapi32", "shell32", "libeay32", "ssleay32", "intl", "iconv", "libpq" }
  else
    links { "pthread", "pq" }
  end
