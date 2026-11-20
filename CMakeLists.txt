cmake_minimum_required(VERSION 3.28)
# The release number: the plugin's version for RED4ext, the build's name
# (core/CMakeLists.txt) and the package's file name (tools/package.py).
project(tandem VERSION 0.2.0 LANGUAGES C CXX)

# No C++ modules here: no dependency scan (CMake's default from C++20 on).
# Scanning preprocesses every file once more, and it compiles with GCC's
# -fmodules-ts, which ccache refuses: no Linux compile would be cached.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ccache refuses every clang-cl /clang: option (it cannot tell what they
# do), and CMake passes two of its own: the standard, for which clang-cl 20
# has /std:c++23preview (the same -std=c++23), and a gcc-style depfile, for
# which cl's /showIncludes does the same for Ninja.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++23preview")
  set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++23preview")
  foreach(lang C CXX)
    set(CMAKE_DEPFILE_FLAGS_${lang} "/showIncludes")
    set(CMAKE_${lang}_DEPFILE_FORMAT msvc)
  endforeach()
endif()

option(TANDEM_BUILD_PLUGIN "Build the game plugin (Windows only)" OFF)
option(TANDEM_BUILD_PEER "Build the mock player and the tests (Linux)" ON)

# ccache in front of every compiler, Windows and Linux alike, unless
# TANDEM_CCACHE=0 (read at each configure; `task build*` configures every
# time) or the caller names a launcher. Its settings come from devenv.nix.
if(NOT "$ENV{TANDEM_CCACHE}" STREQUAL "0")
  find_program(TANDEM_CCACHE_PROGRAM ccache)
  foreach(lang C CXX)
    if(TANDEM_CCACHE_PROGRAM AND NOT DEFINED CACHE{CMAKE_${lang}_COMPILER_LAUNCHER})
      set(CMAKE_${lang}_COMPILER_LAUNCHER "${TANDEM_CCACHE_PROGRAM}")
    endif()
  endforeach()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ENet: reliable and unreliable channels over UDP, the same C code on both sides.
file(GLOB enet_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/vendor/enet/*.c")
add_library(enet STATIC ${enet_sources})
target_include_directories(enet SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/vendor/enet/include")
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
  target_compile_definitions(enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
  target_compile_options(enet PRIVATE -Wno-everything)
else()
  target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
    HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
endif()

add_library(zpp_bits INTERFACE)
target_include_directories(zpp_bits SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/vendor/zpp_bits")

add_subdirectory(core)

if(TANDEM_BUILD_PLUGIN)
  if(NOT WIN32)
    message(FATAL_ERROR "The plugin is a Windows DLL: use the 'windows' preset")
  endif()
  add_subdirectory(plugin)
endif()

if(TANDEM_BUILD_PEER)
  add_subdirectory(peer)
  enable_testing()
  add_subdirectory(tests)
endif()
