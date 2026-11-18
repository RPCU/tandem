# Cross-compiling Windows x64 (MSVC ABI) from Linux with clang-cl and lld-link.
# The MSVC CRT and Windows SDK come from xwin (`task sysroot`), found through
# $TANDEM_XWIN; the compiler paths come from devenv ($TANDEM_CLANG_CL, ...).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

foreach(var TANDEM_XWIN TANDEM_CLANG_CL TANDEM_LLD_LINK TANDEM_LLVM_BIN)
  if(NOT DEFINED ENV{${var}})
    message(FATAL_ERROR "${var} is not set: enter the devenv shell first")
  endif()
endforeach()

set(XWIN "$ENV{TANDEM_XWIN}")
set(CMAKE_C_COMPILER "$ENV{TANDEM_CLANG_CL}")
set(CMAKE_CXX_COMPILER "$ENV{TANDEM_CLANG_CL}")
set(CMAKE_LINKER "$ENV{TANDEM_LLD_LINK}")
set(CMAKE_AR "$ENV{TANDEM_LLVM_BIN}/llvm-lib")
set(CMAKE_RC_COMPILER "$ENV{TANDEM_LLVM_BIN}/llvm-rc")
set(CMAKE_MT "$ENV{TANDEM_LLVM_BIN}/llvm-mt")

set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(_includes
  "${XWIN}/crt/include"
  "${XWIN}/sdk/include/ucrt"
  "${XWIN}/sdk/include/um"
  "${XWIN}/sdk/include/shared")
set(_cflags "")
foreach(dir IN LISTS _includes)
  string(APPEND _cflags " /imsvc \"${dir}\"")
endforeach()
# set, not append: CMake reads a toolchain file more than once.
set(CMAKE_C_FLAGS_INIT "${_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_cflags}")

set(_ldflags
  " /manifest:no"
  " /libpath:\"${XWIN}/crt/lib/x86_64\""
  " /libpath:\"${XWIN}/sdk/lib/um/x86_64\""
  " /libpath:\"${XWIN}/sdk/lib/ucrt/x86_64\"")
string(JOIN "" _ldflags ${_ldflags})
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_ldflags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_ldflags}")

# The plugin carries its own CRT: nothing to install next to the game. Always
# the release CRT: xwin does not download the debug one, and a debug CRT inside
# the game process buys nothing.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_FIND_ROOT_PATH "${XWIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
