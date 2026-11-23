// The RED4ext entry points.

#include "comms.hpp"
#include "config.hpp"
#include "degradations.hpp"
#include "engine/access.hpp"
#include "engine/crowd.hpp"
#include "engine/guards.hpp"
#include "engine/hooks.hpp"
#include "engine/pose.hpp"
#include "engine/posestream.hpp"
#include "engine/quests.hpp"
#include "engine/scenes.hpp"
#include "engine/trace.hpp"
#include "engine/vehiclemove.hpp"
#include "installed.hpp"
#include "log.hpp"
#include "mods.hpp"
#include "observer.hpp"
#include "recording.hpp"
#include "runtime.hpp"
#include "stall.hpp"
#include "story.hpp"
#include "threads.hpp"

#include "tandem/compat.hpp"
#include "tandem/instance.hpp"
#include "tandem/mods.hpp"
#include "tandem/protocol.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

#include <windows.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>

namespace {
const RED4ext::v1::Sdk *g_sdk{};

std::filesystem::path PluginDirectory(RED4ext::v1::PluginHandle handle) {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(reinterpret_cast<HMODULE>(handle), path, MAX_PATH);
  return std::filesystem::path(path).parent_path();
}

// An environment variable of the game's process ("" when not set). Under
// Wine the Unix environment the game was launched with is the process's.
std::wstring Environment(const char *name) {
  const std::wstring wide(name, name + std::strlen(name)); // our names are ASCII
  std::wstring value(32767, L'\0');
  value.resize(GetEnvironmentVariableW(wide.c_str(), value.data(), 32767));
  return value;
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
  tandem::plugin::threads::NoteFrame();
  // The watchdog's sign of life through pauses and loading screens.
  tandem::plugin::Runtime::Get().NoteEngineFrame();
  // What the crash guards skipped on job threads (engine/guards.cpp).
  tandem::plugin::engine::ReportCrashGuards();
  // The disposals the scripts asked for (Traffic.reds Remove), here.
  tandem::plugin::engine::RunDisposals();
  // Our crowd's spawners, off where another game runs the street.
  tandem::plugin::engine::RunCrowdSpawners();
  // Once, at the first frame: what of the engine this game lets us use
  // (engine/access.hpp). A blocked plugin touches nothing and checks nothing.
  if (!tandem::plugin::engine::AccessChecked() && !tandem::plugin::Runtime::Get().Blocked())
    tandem::plugin::engine::CheckAccess();
  // A core feature went off (the check above, or a first use later): a
  // session chosen and not started is dropped, the menu says why.
  static std::uint64_t s_degradedSeen = 0;
  if (const auto changes = tandem::plugin::Degradations::Get().Local().Changes();
      changes != s_degradedSeen) {
    s_degradedSeen = changes;
    tandem::plugin::Runtime::Get().RecheckChoice();
  }
  if (!tandem::plugin::Runtime::Get().Started())
    return false;
  Red::ScriptGameInstance game;
  static ScriptFrame pause{"TandemPause", "pause"};
  pause.Call(game);
  return false; // ignored for the running state
}

// What is installed around us, judged and logged once (tandem/compat.hpp):
// the game's version, the mods tandem needs, tandem's own files, and what the
// other players will be told. A problem keeps co-op off; the co-op menu
// says why.
void CheckInstallation(tandem::plugin::Runtime &runtime, const std::filesystem::path &plugin) {
  using namespace tandem;
  using namespace tandem::plugin;
  auto &found = runtime.installation;
  found.game = GameDirectoryOf(plugin);
  found.facts = GatherFacts(found.game, ModulePath(nullptr));
  found.startup = compat::Check(found.facts);
  found.install = compat::LocalInstall(found.game, found.facts.game);
  log::Info("game {} (built for patch {}, {})",
            found.facts.game ? compat::Format(*found.facts.game) : "version not read",
            compat::kGamePatch, compat::Format(compat::kGame));
  for (const auto &mod : found.facts.mods)
    log::Info("{}: {}", mod.name,
              !mod.present  ? "not installed"
              : mod.version ? compat::Format(*mod.version)
                            : "installed, version not read");
  log::Info("tandem scripts {:016x}, tweaks {:016x}; {} other mod(s), list {:016x}",
            found.install.scripts, found.install.tweaks, found.install.modCount,
            found.install.mods);
  // What the other mods add (docs/architecture/mod-compat.md): compared with
  // the other players' at a join.
  auto ours = mods::Scan(found.game);
  std::array<std::size_t, mods::kCategoryOrder.size()> by{};
  for (const auto &mod : ours)
    for (std::size_t i = 0; i < by.size(); ++i)
      by[i] += mods::Primary(mod.categories) == mods::kCategoryOrder[i];
  std::string sorted;
  for (std::size_t i = 0; i < by.size(); ++i)
    if (by[i])
      sorted += std::format("{}{} {}", sorted.empty() ? "" : ", ", by[i],
                            mods::CategoryName(mods::kCategoryOrder[i]));
  log::Info("other mods by what they add: {} ({})", ours.size(), sorted.empty() ? "none" : sorted);
  ModRecords::Get().SetOurs(std::move(ours));
  for (const auto &warning : found.startup.warnings)
    log::Warn("startup check: {}", warning);
  for (const auto &problem : found.startup.problems)
    log::Error("startup check: {}", problem);
  if (found.startup.Blocked() && !runtime.config.compat)
    log::Warn("startup check: ignored (compat = off in tandem.ini)");
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
    // One game among several on a machine keeps its files apart and takes
    // its own port (tandem/instance.hpp, docs/architecture/two-instances.md).
    const auto files = tandem::instance::Resolve(
        PluginDirectory(handle), {.config = Environment(tandem::instance::kConfigVariable),
                                  .command = Environment(tandem::instance::kCommandVariable),
                                  .log = Environment(tandem::instance::kLogVariable)});
    const bool ownLog = files.log.empty() || log::OpenFile(files.log);
    log::Info("tandem {} loaded (protocol {})", tandem::compat::Build(),
              tandem::proto::kProtocolVersion);
    if (!ownLog)
      log::Warn("{}: cannot write {}", tandem::instance::kLogVariable, files.log.string());
    if (files != tandem::instance::Resolve(PluginDirectory(handle), {}))
      log::Info("instance files: settings {}, commands {}, log {}", files.config.string(),
                files.command.string(), files.log.empty() ? "RED4ext's" : files.log.string());

    auto &runtime = Runtime::Get();
    // A release ships its defaults as tandem.default.ini and never the
    // player's own tandem.ini: the first start makes it, an update keeps it.
    {
      const auto defaults = PluginDirectory(handle) / "tandem.default.ini";
      std::error_code ec;
      if (!std::filesystem::exists(files.config, ec) && std::filesystem::exists(defaults, ec) &&
          std::filesystem::copy_file(defaults, files.config, ec))
        log::Info("{} created from tandem.default.ini", files.config.filename().string());
    }
    runtime.config = LoadConfig(files.config);
    runtime.directOn = runtime.config.direct;
    runtime.codeSalt = runtime.config.codeSalt;
    runtime.config.commandFile = files.command;
    runtime.config.recentFile = files.recent;
    tandem::plugin::ConfigureComms(runtime.config);
    log::SetLevel(static_cast<log::Level>(runtime.config.logLevel));
    if (const auto text = Environment(tandem::instance::kPortVariable); !text.empty()) {
      // Digits are ASCII; anything else is refused by ParsePort.
      std::string narrow;
      for (const wchar_t c : text)
        narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
      if (const auto port = tandem::instance::ParsePort(narrow)) {
        runtime.config.port = *port;
        log::Info("port {} ({})", *port, tandem::instance::kPortVariable);
      } else {
        log::Warn("{}: bad port {}", tandem::instance::kPortVariable, narrow);
      }
    }
    CheckInstallation(runtime, PluginDirectory(handle));
    runtime.ResetChoice();
    // A command left from an earlier run is not for this one.
    std::error_code ignored;
    std::filesystem::remove(runtime.config.commandFile, ignored);
    // Nor is a pilot state: the test pilot (tools/pilot) must not read an old
    // one. Next to the command file, so each instance (TANDEM_CMD) has its own.
    runtime.config.pilotStateFile = files.command.parent_path() / "pilot.state";
    std::filesystem::remove(runtime.config.pilotStateFile, ignored);
    // The stall report (stall.hpp): beside the command file, each instance its own.
    stall::Start(files.command.parent_path() / "stall.log");
    // The observer (observer.hpp): its own key, whatever the role; beside the
    // command file too, each instance its own.
    Observer::Get().Configure({.enabled = runtime.config.observer,
                               .dir = files.command.parent_path() / "observer",
                               .driftMetres = runtime.config.observerDriftM,
                               .frameMs = runtime.config.observerFrameMs,
                               .maxCaptures = runtime.config.observerCaptures,
                               .maxBytes = std::uint64_t{runtime.config.observerMaxMb} << 20});
    log::Info("role: {} (tandem.ini; the co-op menu can pick another)",
              NameOf(runtime.config.role));

    Red::TypeInfoRegistrar::RegisterDiscovered();
    // A startup problem: the natives stay (the scripts call them, the menu
    // says what is wrong) and nothing else touches the game.
    if (runtime.Blocked()) {
      log::Error("co-op is off: no hook installed, the game runs as without tandem");
      break;
    }
    // Installed later, from the game thread (TandemNative.PoseHook).
    engine::SetPoseHookContext(sdk, handle);
    // Whatever the role: the co-op menu can start a session at any time, and
    // hooking a running game races its other threads. Without a session the
    // hooks only pass through (no puppet registered, no pose source).
    engine::InstallHooks(sdk, handle);
    // Proton only: an engine crash of its own, made likelier by our proxies'
    // deletions (engine/guards.cpp). Whatever the role: solo plays too.
    const auto guard = engine::InstallCrashGuards(sdk, handle);
    // Every platform: an NPC detached in the middle of an action (re.md
    // §13.142), which our proxies' deletions make likelier too.
    const auto dismemberment = engine::InstallDismembermentGuard(sdk, handle);
    // Every platform: a slide handed an entity that is not a puppet (an NPC
    // proxy mounted in a vehicle copy, engine/npcs.md §13.154).
    const auto slide = engine::InstallSlideGuard(sdk, handle);
    sdk->gameStates->Add(handle, RED4ext::EGameStateType::Running, &g_running);
    engine::AskVehicleMove(runtime.config.vehicleMove);
    if (runtime.config.pose)
      engine::InstallPoseStream(sdk, handle);
    else
      engine::FeatureSwitchedOff(engine::feature::kPoseStream, "tandem.ini pose = off");
    // The quest node hook (engine/quests.cpp): the guest's quest freeze, only
    // when asked for, and which scene each game plays (scene privacy).
    // Whatever the role (a session may be chosen in the co-op menu): it
    // passes through until a session turns it on.
    if (runtime.config.questFreeze || runtime.config.scenePrivacy || runtime.config.sceneReplay) {
      engine::SetScenePrivacy({runtime.config.privateActors, runtime.config.privateScenes,
                               runtime.config.publicScenes});
      engine::InstallQuestFreeze(sdk, handle, runtime.config.questFreezeAllow,
                                 runtime.config.questFreezeGigs, runtime.config.questFreeze);
    } else {
      const char *why = "tandem.ini quest_freeze, scene_privacy and scene_replay off";
      engine::FeatureNotAsked(engine::feature::kQuestNodes, why);
      engine::FeatureSwitchedOff(engine::feature::kQuestFreeze, why);
    }
    // The scripted moments marked by hand (docs/architecture/scripted-moments.md,
    // "The moment list"): shipped beside the DLL, plus tandem.ini's.
    LoadMoments(PluginDirectory(handle) / "moments.txt");
    if (runtime.config.trace || runtime.config.mirror)
      engine::InstallTrace(sdk, handle, runtime.config.trace);
    else
      engine::FeatureNotAsked(engine::feature::kTrace, "tandem.ini trace and mirror off");
    // The recorder (recording.hpp): its own key, whatever the role.
    Recording::Get().Configure(runtime.config.recorder,
                               PluginDirectory(handle) / "recordings" / "auto",
                               runtime.config.recorderMaxMb << 20,
                               std::uint64_t{runtime.config.recorderSegmentMinutes} * 60'000'000);
    // What is installed, and that none of it acts without a session
    // (docs/architecture/session.md, "Solo is vanilla").
    {
      std::string hooks = "idle-controller pass-through";
      if (runtime.config.pose)
        hooks += ", pose stream (reads only with a source)";
      if (runtime.config.questFreeze || runtime.config.scenePrivacy)
        hooks += ", quest node observer (freeze off)";
      if (runtime.config.trace || runtime.config.mirror)
        hooks += ", anim trace";
      log::Info("tandem: {} (hooks idle: {}; script wraps gated on a session); recorder {}; "
                "transform guard {}; dismemberment guard {}; slide guard {}",
                runtime.config.role == Config::Role::Off
                    ? std::string("solo, passive")
                    : std::format("co-op at load as {}", NameOf(runtime.config.role)),
                hooks, runtime.config.recorder ? "on" : "off", guard, dismemberment, slide);
    }
    // The session starts when scripts report a loaded game (TandemNative.Start).
    break;
  }
  case RED4ext::v1::EMainReason::Unload:
    Observer::Get().Shutdown();
    stall::Stop();
    Runtime::Get().Stop();
    Recording::Get().Shutdown();
    engine::RemoveHooks(g_sdk, handle);
    engine::RemoveCrashGuards(g_sdk, handle);
    engine::RemovePoseStream(g_sdk, handle);
    engine::RemoveTrace(g_sdk, handle);
    engine::RemoveQuestFreeze(g_sdk, handle);
    engine::RemovePoseHook();
    log::Info("tandem unloaded");
    log::g_logger = nullptr;
    log::CloseFile();
    break;
  }
  return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo *info) {
  info->name = L"tandem";
  info->author = L"tandem";
  info->version =
      RED4EXT_V1_SEMVER(TANDEM_VERSION_MAJOR, TANDEM_VERSION_MINOR, TANDEM_VERSION_PATCH);
  // Any game version: RED4ext would leave a plugin made for another patch
  // out without a word in game, and our scripts would then call natives
  // that are not there. We load, check the version ourselves
  // (CheckInstallation, against the same file version RED4ext compares)
  // and stay inert with a message in the co-op menu.
  info->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
  info->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT std::uint32_t RED4EXT_CALL Supports() { return RED4EXT_API_VERSION_1; }
