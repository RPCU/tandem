// The RED4ext entry points.

#include "config.hpp"
#include "engine/hooks.hpp"
#include "engine/pose.hpp"
#include "engine/posestream.hpp"
#include "engine/quests.hpp"
#include "engine/scenes.hpp"
#include "engine/trace.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/protocol.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

#include <windows.h>

#include <filesystem>
#include <format>

namespace {
const RED4ext::v1::Sdk *g_sdk{};

std::filesystem::path PluginDirectory(RED4ext::v1::PluginHandle handle) {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(reinterpret_cast<HMODULE>(handle), path, MAX_PATH);
  return std::filesystem::path(path).parent_path();
}

// Every frame of the running game, on the game thread, also while the world
// is paused (scripts' delayed callbacks then stand still): the pause script
// follows the other players' pause menus (scripts/tandem/Pause.reds), and
// can let go of a pause from there. Only that: this runs outside the script
// tick, also behind the loading screen (slowed time runs from
// TandemSystem.Tick instead, scripts/tandem/Time.reds).
// A script class's static function, found once: a script class's static
// functions are neither in its member nor its static list, they are
// registered as global functions under a qualified name
// (docs/research/engine-knowledge.md §13.13).
struct ScriptFrame {
  const char *script;
  const char *what; // the log's word for it
  RED4ext::CBaseFunction *function{};
  bool searched{};
  std::uint32_t failures{};

  void Call(Red::ScriptGameInstance &game) {
    if (!function && !searched && Red::GetClass(script)) {
      searched = true;
      RED4ext::DynArray<RED4ext::CBaseFunction *> globals;
      RED4ext::CRTTISystem::Get()->GetGlobalFunctions(globals);
      std::string seen;
      for (auto *f : globals) {
        const std::string_view full = f->fullName.ToString();
        if (full.find(script) != std::string_view::npos) {
          seen += std::format(" {}", full);
          if (full.find("Frame") != std::string_view::npos)
            function = f;
        }
      }
      tandem::plugin::log::Info("{}: {} globals:{}; frame {}", what, script, seen,
                                function != nullptr);
    }
    // Reported on the first failure and every 1000 after: the scripts may
    // not be there in the first frames.
    if ((!function || !Red::CallFunction(function, game)) && failures++ % 1000 == 0)
      tandem::plugin::log::Warn("{}: {}.Frame could not be called ({} times)", what, script,
                                failures);
  }
};

bool OnRunningUpdate(RED4ext::CGameApplication *) {
  if (!tandem::plugin::Runtime::Get().Started())
    return false;
  Red::ScriptGameInstance game;
  static ScriptFrame pause{"TandemPause", "pause"};
  pause.Call(game);
  return false; // ignored for the running state
}

bool OnRunningEdge(RED4ext::CGameApplication *) { return true; }
RED4ext::v1::GameState g_running{&OnRunningEdge, &OnRunningUpdate, &OnRunningEdge};
} // namespace

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle handle,
                                        RED4ext::v1::EMainReason reason,
                                        const RED4ext::v1::Sdk *sdk) {
  using namespace tandem::plugin;

  switch (reason) {
  case RED4ext::v1::EMainReason::Load: {
    g_sdk = sdk;
    log::g_handle = handle;
    log::g_logger = sdk->logger;
    log::Info("tandem {} loaded (protocol {})", TANDEM_VERSION, tandem::proto::kProtocolVersion);

    auto &runtime = Runtime::Get();
    runtime.config = LoadConfig(PluginDirectory(handle) / "tandem.ini");
    runtime.config.commandFile = PluginDirectory(handle) / "tandem.cmd";
    runtime.config.recentFile = PluginDirectory(handle) / "recent.txt";
    runtime.ResetChoice();
    // A command left from an earlier run is not for this one.
    std::error_code ignored;
    std::filesystem::remove(runtime.config.commandFile, ignored);
    log::Info("role: {} (tandem.ini; the co-op menu can pick another)",
              NameOf(runtime.config.role));

    Red::TypeInfoRegistrar::RegisterDiscovered();
    // Installed later, from the game thread (TandemNative.PoseHook).
    engine::SetPoseHookContext(sdk, handle);
    // Whatever the role: the co-op menu can start a session at any time, and
    // hooking a running game races its other threads. Without a session the
    // hooks only pass through (no puppet registered, no pose source).
    engine::InstallHooks(sdk, handle);
    sdk->gameStates->Add(handle, RED4ext::EGameStateType::Running, &g_running);
    if (runtime.config.pose)
      engine::InstallPoseStream(sdk, handle);
    // The quest node hook (engine/quests.cpp): the guest's quest freeze, only
    // when asked for, and which scene each game plays (scene privacy).
    // Whatever the role (a session may be chosen in the co-op menu): it
    // passes through until a session turns it on.
    if (runtime.config.questFreeze || runtime.config.scenePrivacy || runtime.config.sceneReplay) {
      engine::SetScenePrivacy({runtime.config.privateActors, runtime.config.privateScenes,
                               runtime.config.publicScenes});
      engine::InstallQuestFreeze(sdk, handle, runtime.config.questFreezeAllow,
                                 runtime.config.questFreeze);
    }
    if (runtime.config.trace || runtime.config.mirror)
      engine::InstallTrace(sdk, handle, runtime.config.trace);
    // The session starts when scripts report a loaded game (TandemNative.Start).
    break;
  }
  case RED4ext::v1::EMainReason::Unload:
    Runtime::Get().Stop();
    engine::RemoveHooks(g_sdk, handle);
    engine::RemovePoseStream(g_sdk, handle);
    engine::RemoveTrace(g_sdk, handle);
    engine::RemoveQuestFreeze(g_sdk, handle);
    engine::RemovePoseHook();
    log::Info("tandem unloaded");
    log::g_logger = nullptr;
    break;
  }
  return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo *info) {
  info->name = L"tandem";
  info->author = L"tandem";
  info->version = RED4EXT_V1_SEMVER(0, 2, 0);
  info->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
  info->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT std::uint32_t RED4EXT_CALL Supports() { return RED4EXT_API_VERSION_1; }
