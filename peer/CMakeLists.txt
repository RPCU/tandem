# The simulated second game (docs/architecture/sim.md): the referee, the
# world and player models, the personas. A library of its own so the tests
# run it without a network; never part of the plugin.
add_library(tandem_sim STATIC
  sim/src/referee.cpp
  sim/src/coverage.cpp
  sim/src/scene.cpp
  sim/src/persona.cpp
  sim/src/motion.cpp
  sim/src/player.cpp
  sim/src/world.cpp
  sim/src/story.cpp
  sim/src/game.cpp)
target_include_directories(tandem_sim PUBLIC sim/include)
target_link_libraries(tandem_sim PUBLIC tandem_core)

add_executable(tandem-peer src/main.cpp)
target_link_libraries(tandem-peer PRIVATE tandem_core tandem_sim)
