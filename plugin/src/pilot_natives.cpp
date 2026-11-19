// What scripts/tandem/Pilot.reds can call: `TandemPilotNative` static
// functions (docs/architecture/autopilot.md). Declared on the script side in
// scripts/tandem/PilotNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread, and only while the test pilot is on.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "runtime.hpp"

#include <RedLib.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace tandem::plugin {
namespace {
bool g_savesLocked = false; // game thread only
}

struct TandemPilotNative : Red::IScriptable {
  // The pilot state, one line, for tandem-pilot beside the game: written to
  // pilot.state next to the DLL, replaced whole (a rename), so a reader
  // never sees half a line.
  static bool WriteState(const Red::CString &line) {
    const auto &path = Runtime::Get().config.pilotStateFile;
    if (path.empty())
      return false;
    auto temporary = path;
    temporary += ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      if (!file)
        return false;
      file << line.c_str() << '\n';
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
  }

  // The pilot went off: no state for a reader to mistake for a live one.
  static void ClearState() {
    const auto &path = Runtime::Get().config.pilotStateFile;
    std::error_code error;
    if (!path.empty())
      std::filesystem::remove(path, error);
  }

  // Saving locked for the rest of this run of the game: set by the first
  // "pilot on", read by TandemSystem.IsSavingLocked. In the process, not in
  // a script object: a loaded save builds the script systems anew.
  static void LockSaves() {
    if (!g_savesLocked)
      log::Info("pilot: saving is locked until the game ends");
    g_savesLocked = true;
  }
  static bool SavesLocked() { return g_savesLocked; }

  // (probe, read only) What the game's own functional-test system offers
  // in this build: whether its class is in RTTI, whether the running game
  // has an instance of it, and its functions with their parameters, in
  // tandem's log. Its FakeInput* functions would be input with no device at
  // all; nothing is called here. Answers a one-line summary.
  static Red::CString ProbeFunctionalTests() {
    auto *rtti = Red::CRTTISystem::Get();
    auto *engine = Red::CGameEngine::Get();
    std::string summary;
    for (const char *name : {"FunctionalTestsGameSystem", "PlayerFunctionalTests"}) {
      auto *type = rtti->GetClassByScriptName(name);
      if (!type)
        type = rtti->GetClass(name);
      if (!type) {
        log::Info("pilot probe: no class {}", name);
        summary += std::string(name) + "=none ";
        continue;
      }
      void *system = nullptr;
      if (engine && engine->framework && engine->framework->gameInstance)
        system = engine->framework->gameInstance->GetSystem(type);
      log::Info("pilot probe: class {} (native {}), {} functions, instance {}", name,
                type->GetName().ToString(), type->funcs.Size() + type->staticFuncs.Size(),
                system ? "present" : "absent");
      summary += std::string(name) + (system ? "=present " : "=absent ");
      const auto describe = [name](const Red::CBaseFunction *function, const char *kind) {
        if (!function)
          return;
        std::string text;
        for (const auto *param : function->params) {
          if (!param)
            continue;
          if (!text.empty())
            text += ", ";
          text += param->name.ToString();
          text += ": ";
          text += param->type ? param->type->GetName().ToString() : "?";
          if (param->flags.isOut)
            text += " (out)";
          if (param->flags.isOptional)
            text += " (opt)";
        }
        const char *result = function->returnType && function->returnType->type
                                 ? function->returnType->type->GetName().ToString()
                                 : "Void";
        log::Info("pilot probe: {}{}.{}({}) -> {}", kind, name, function->shortName.ToString(),
                  text, result);
      };
      for (const auto *function : type->funcs)
        describe(function, "");
      for (const auto *function : type->staticFuncs)
        describe(function, "static ");
    }
    return Red::CString(summary.c_str());
  }

  RTTI_IMPL_TYPEINFO(TandemPilotNative);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemPilotNative, "TandemPilotNative", {
  RTTI_METHOD(WriteState);
  RTTI_METHOD(ClearState);
  RTTI_METHOD(LockSaves);
  RTTI_METHOD(SavesLocked);
  RTTI_METHOD(ProbeFunctionalTests);
});
