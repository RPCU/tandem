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
  src/instance.cpp
  src/progress.cpp
  src/trade.cpp)
target_include_directories(tandem_core PUBLIC include)
target_link_libraries(tandem_core PUBLIC enet zpp_bits)
target_compile_features(tandem_core PUBLIC cxx_std_23)
# zpp_bits checks the ids of the message variant's alternatives at compile
# time, in steps that grow with the square of their number: from the 89th
# message on, clang's default budget (1048576 steps) runs out ("constexpr
# evaluation hit maximum step limit"). GCC's limit is far higher.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(MSVC)
    target_compile_options(tandem_core PUBLIC /clang:-fconstexpr-steps=16777216)
  else()
    target_compile_options(tandem_core PUBLIC -fconstexpr-steps=16777216)
  endif()
endif()
