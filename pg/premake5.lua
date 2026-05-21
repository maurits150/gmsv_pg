workspace "pg"
  location "./project"
  configurations { "Release" }
  flags { "NoPCH", "NoImportLib"}
  symbols "On"
  editandcontinue "Off"
  vectorextensions "SSE"

  if os.target() ~= 'windows' then
    linkoptions{ "-static-libstdc++", "-static-libgcc" }
  end

  configuration 'Release'
    defines { 'NDEBUG' }
    optimize "On"
    floatingpoint "Fast"
    architecture 'x86'
project "pg"
  kind "SharedLib"
  language "C++"
  location "./project"
  targetdir "./bin"
  libdirs { 'lib/'..os.target() }
  if os.target() == 'windows' then
    includedirs { 'include/libpq', '../vendor/gmod-lua' }
  else
    includedirs { '/usr/include/postgresql', '../vendor/gmod-lua' }
  end

  files {
    "src/lua/**.cpp",
    "src/lua/**.h",
    "src/postgres/**.cpp",
    "src/postgres/**.h",
    "src/BlockingQueue.h"
  }

  include "../premake5.lua"

  if os.target() == 'windows' then
    links { 'ws2_32', 'wsock32', 'secur32', 'wldap32', 'advapi32', 'shell32', 'libeay32', 'ssleay32', 'intl', 'iconv', 'libpq' }
  else
    pic "On"
    links { 'pthread', 'pq' }
  end
