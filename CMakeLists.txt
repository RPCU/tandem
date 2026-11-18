cmake_minimum_required(VERSION 3.28)
project(tandem VERSION 0.1.0 LANGUAGES C CXX)

option(TANDEM_BUILD_PLUGIN "Build the game plugin (Windows only)" OFF)
option(TANDEM_BUILD_PEER "Build the mock player and the tests (Linux)" ON)

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
