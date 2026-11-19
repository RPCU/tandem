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
  src/addresses.cpp
  src/recording.cpp
  src/story.cpp
  src/story_natives.cpp
  src/elsewhere_natives.cpp
  src/activity.cpp
  src/activity_natives.cpp
  src/combat.cpp
  src/combat_natives.cpp
  src/sync_natives.cpp
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
target_link_libraries(tandem PRIVATE tandem_core RED4ext::SDK iphlpapi)
target_include_directories(tandem SYSTEM PRIVATE
  "${CMAKE_BINARY_DIR}/sdk-casefix"
  "${CMAKE_SOURCE_DIR}/vendor/RedLib/include"
  "${CMAKE_SOURCE_DIR}/vendor/RedLib/vendor")
target_compile_definitions(tandem PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX
  TANDEM_VERSION="${PROJECT_VERSION}")
# The SDK and RedLib are written for MSVC; keep clang-cl quiet about them.
target_compile_options(tandem PRIVATE -Wno-microsoft-include -Wno-pragma-pack -Wno-unknown-pragmas
  -Wno-invalid-offsetof)
set_target_properties(tandem PROPERTIES OUTPUT_NAME "tandem" PREFIX "")
