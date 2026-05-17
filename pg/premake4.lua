local target_os = os.get()
local suffixes = {
  linux   = "_linux",
  macosx  = "_macosx",
  windows = "_win32",
}

solution "pg"
  location "./project"
  configurations { "Release" }
  flags { "NoPCH", "NoImportLib", "Symbols", "NoEditAndContinue", "EnableSSE" }

  if target_os ~= "windows" then
    buildoptions { "-m32", "-fPIC" }
    linkoptions { "-m32", "-static-libstdc++" }
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
    "include",
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
    links { "ws2_32", "libeay32", "libpqxx_static", "libpq" }
  else
    links { "pthread", "pqxx", "pq" }
  end
