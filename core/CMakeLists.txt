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
  src/combat.cpp
  src/interpolation.cpp
  src/movement.cpp
  src/play.cpp
  src/pose.cpp
  src/vehicle.cpp
  src/hosts.cpp
  src/timebubble.cpp
  src/shaping.cpp
  src/elsewhere.cpp
  src/instance.cpp)
target_include_directories(tandem_core PUBLIC include)
target_link_libraries(tandem_core PUBLIC enet zpp_bits)
target_compile_features(tandem_core PUBLIC cxx_std_23)
