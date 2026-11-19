# Platform-independent: the protocol and the session over ENet. Built into the
# game plugin (Windows) and into the mock player and the tests (Linux), so both
# sides always speak the same protocol.
add_library(tandem_core STATIC
  src/protocol.cpp
  src/session.cpp
  src/relay.cpp
  src/consistency.cpp
  src/endpoint.cpp
  src/endpoint_regions.cpp
  src/story.cpp
  src/carryover.cpp
  src/scene.cpp
  src/combat.cpp
  src/interpolation.cpp
  src/movement.cpp
  src/play.cpp
  src/recorder.cpp
  src/pose.cpp
  src/vehicle.cpp
  src/hosts.cpp
  src/timebubble.cpp
  src/shaping.cpp
  src/elsewhere.cpp
  src/compat.cpp
  src/report.cpp
  src/instance.cpp
  src/devices.cpp
  src/regions.cpp
  src/traffic.cpp
  src/presence.cpp)
target_include_directories(tandem_core PUBLIC include)
target_link_libraries(tandem_core PUBLIC enet zpp_bits)
target_compile_features(tandem_core PUBLIC cxx_std_23)
# zpp_bits checks at compile time that the ids of the message variant are
# unique, by recursion over every pair of alternatives: past some 75 messages
# clang runs out of its default constexpr steps (GCC's limit is higher).
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(tandem_core PRIVATE /clang:-fconstexpr-steps=33554432)
  else()
    target_compile_options(tandem_core PRIVATE -fconstexpr-steps=33554432)
  endif()
endif()

# The build's name, "0.2.0+2fa03ca": the release and the commit it was built
# from (the release alone outside git; TANDEM_BUILD_ID replaces it). Players
# whose builds differ are told at a join (tandem/compat.hpp).
set(TANDEM_BUILD_ID "" CACHE STRING "The build's name, instead of release+commit")
set(TANDEM_BUILD "${TANDEM_BUILD_ID}")
if(NOT TANDEM_BUILD)
  set(TANDEM_BUILD "${PROJECT_VERSION}")
  # A copy of the tree without its history (tools/remote.sh) is told the
  # commit it was copied from.
  set(_commit "$ENV{TANDEM_COMMIT}")
  find_package(Git QUIET)
  if(NOT _commit AND GIT_EXECUTABLE)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse --short=7 HEAD
      OUTPUT_VARIABLE _commit OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET RESULT_VARIABLE _failed)
    if(_failed)
      set(_commit "")
    endif()
  endif()
  if(_commit)
    string(APPEND TANDEM_BUILD "+${_commit}")
  endif()
endif()
configure_file(src/tandem_build.hpp.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/tandem_build.hpp" @ONLY)
target_include_directories(tandem_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
