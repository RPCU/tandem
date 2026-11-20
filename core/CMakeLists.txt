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
  src/traffic.cpp
  src/netcode.cpp
  src/net_traffic.cpp
  src/interact.cpp
  src/lagcomp.cpp
  src/elsewhere.cpp
  src/compat.cpp
  src/report.cpp
  src/instance.cpp
  src/traffic.cpp
  src/vehicle_combat.cpp
  src/endpoint_vehicles.cpp
  src/devices.cpp
  src/regions.cpp
  src/presence.cpp
  src/settings.cpp
  src/activity.cpp
  src/progress.cpp
  src/trade.cpp
  src/comms.cpp
  src/names.cpp)
target_include_directories(tandem_core PUBLIC include)
target_link_libraries(tandem_core PUBLIC enet zpp_bits)
target_compile_features(tandem_core PUBLIC cxx_std_23)
# zpp_bits checks at compile time that every message of the variant has its
# own id, in steps that grow with the square of their number: past some 75
# messages clang runs out of its default constexpr steps (1048576), and GCC
# of its ops limit further on. PUBLIC: what instantiates the encoding needs it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(tandem_core PUBLIC /clang:-fconstexpr-steps=33554432)
  else()
    target_compile_options(tandem_core PUBLIC -fconstexpr-steps=33554432)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(tandem_core PUBLIC -fconstexpr-ops-limit=1073741824)
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
