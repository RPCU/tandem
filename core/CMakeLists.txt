# Platform-independent: the protocol and the session over ENet. Built into the
# game plugin (Windows) and into the mock player and the tests (Linux), so both
# sides always speak the same protocol.
add_library(tandem_core STATIC
  src/protocol.cpp
  src/session.cpp
  src/relay.cpp
  src/consistency.cpp
  src/endpoint.cpp
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
  src/activity.cpp
  src/instance.cpp)
target_include_directories(tandem_core PUBLIC include)
target_link_libraries(tandem_core PUBLIC enet zpp_bits)
target_compile_features(tandem_core PUBLIC cxx_std_23)
# zpp_bits checks at compile time that every message of the variant has its
# own id, in steps that grow with the square of their number: past about 80
# messages that outruns clang's default constexpr budget (1048576 steps).
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(tandem_core PUBLIC /clang:-fconstexpr-steps=16777216)
  else()
    target_compile_options(tandem_core PUBLIC -fconstexpr-steps=16777216)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(tandem_core PUBLIC -fconstexpr-ops-limit=1073741824)
endif()
