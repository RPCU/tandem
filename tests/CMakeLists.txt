add_executable(tandem-tests main.cpp protocol_test.cpp compat_test.cpp report_test.cpp endpoint_test.cpp interpolation_test.cpp movement_test.cpp play_test.cpp pose_test.cpp vehicle_test.cpp story_test.cpp hosts_test.cpp recorder_test.cpp timebubble_test.cpp relay_test.cpp shaping_test.cpp regroup_test.cpp combat_test.cpp consistency_test.cpp vanish_test.cpp elsewhere_test.cpp instance_test.cpp carryover_test.cpp scene_test.cpp devices_test.cpp breach_test.cpp regions_test.cpp regions_endpoint_test.cpp traffic_test.cpp police_test.cpp presence_test.cpp settings_test.cpp activity_test.cpp progress_test.cpp trade_test.cpp comms_test.cpp comms_hacks_test.cpp vehicle_combat_test.cpp restrictions_test.cpp referee_test.cpp sim_test.cpp netcode_test.cpp quality_test.cpp budget_test.cpp reliability_test.cpp names_test.cpp)
target_link_libraries(tandem-tests PRIVATE tandem_core tandem_sim)
target_include_directories(tandem-tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/vendor/doctest")
add_test(NAME tandem-tests COMMAND tandem-tests)
add_test(NAME peer-flags COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_flags.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-replay COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_replay.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-play COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_play.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-devices COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_devices.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-breach COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_breach.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-regions COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_regions.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-street COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_street.sh" $<TARGET_FILE:tandem-peer>)
add_test(NAME peer-wheels COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/peer_wheels.sh" $<TARGET_FILE:tandem-peer>)

# Sessions through a real relay process (built from relay/ with Go).
find_program(GO_EXECUTABLE go)
if(GO_EXECUTABLE)
  add_test(NAME relay-loopback
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/relay_loopback.sh" "${GO_EXECUTABLE}"
                   "${CMAKE_SOURCE_DIR}/relay" $<TARGET_FILE:tandem-tests> $<TARGET_FILE:tandem-peer>)
else()
  message(WARNING "go not found: the relay-loopback test is left out")
endif()
