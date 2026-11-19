set(RED4EXT_HEADER_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/vendor/RED4ext.SDK" "${CMAKE_BINARY_DIR}/RED4ext.SDK" EXCLUDE_FROM_ALL)
# Its headers are not ours to warn about.
set_property(TARGET RED4ext.SDK PROPERTY SYSTEM TRUE)

# The SDK is written on Windows, where paths ignore case: a few includes say
# RED4ext/rtti/ for the RED4ext/RTTI/ directory. A link in the build tree makes
# them resolve on Linux without touching the vendored SDK.
set(_casefix "${CMAKE_BINARY_DIR}/sdk-casefix/RED4ext")
file(MAKE_DIRECTORY "${_casefix}")
file(CREATE_LINK "${CMAKE_SOURCE_DIR}/vendor/RED4ext.SDK/include/RED4ext/RTTI" "${_casefix}/rtti" SYMBOLIC)

add_library(tandem SHARED
  src/main.cpp
  src/config.cpp
  src/log.cpp
  src/net_thread.cpp
  src/runtime.cpp
  src/natives.cpp
  src/installed.cpp
  src/addresses.cpp
  src/recording.cpp
  src/story.cpp
  src/story_natives.cpp
  src/elsewhere_natives.cpp
  src/presence.cpp
  src/presence_natives.cpp
  src/activity.cpp
  src/activity_natives.cpp
  src/combat.cpp
  src/combat_natives.cpp
  src/sync_natives.cpp
  src/street_natives.cpp
  src/wheels_natives.cpp
  src/pilot_natives.cpp
  src/regions.cpp
  src/region_natives.cpp
  src/progress_natives.cpp
  src/trade.cpp
  src/trade_natives.cpp
  src/comms.cpp
  src/comms_natives.cpp
  src/engine/layout.cpp
  src/engine/animation.cpp
  src/engine/controller.cpp
  src/engine/hooks.cpp
  src/engine/look.cpp
  src/engine/trace.cpp
  src/engine/pose.cpp
  src/engine/posescan.cpp
  src/engine/posestream.cpp
  src/engine/quests.cpp
  src/engine/scenes.cpp)
target_link_libraries(tandem PRIVATE tandem_core RED4ext::SDK iphlpapi version)
target_include_directories(tandem SYSTEM PRIVATE
  "${CMAKE_BINARY_DIR}/sdk-casefix"
  "${CMAKE_SOURCE_DIR}/vendor/RedLib/include"
  "${CMAKE_SOURCE_DIR}/vendor/RedLib/vendor")
target_compile_definitions(tandem PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX
  TANDEM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  TANDEM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  TANDEM_VERSION_PATCH=${PROJECT_VERSION_PATCH})
# The SDK and RedLib are written for MSVC; keep clang-cl quiet about them.
target_compile_options(tandem PRIVATE -Wno-microsoft-include -Wno-pragma-pack -Wno-unknown-pragmas
  -Wno-invalid-offsetof)
set_target_properties(tandem PROPERTIES OUTPUT_NAME "tandem" PREFIX "")

# tandem-report.exe: the bug report zip when the game does not start (the
# co-op menu writes the same one from inside the game). Installed next to
# the DLL; no engine code, only core/ and the installed-versions reader.
add_executable(tandem-report report/main.cpp src/installed.cpp)
target_link_libraries(tandem-report PRIVATE tandem_core version)
target_compile_definitions(tandem-report PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
