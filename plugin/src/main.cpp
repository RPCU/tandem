// The RED4ext entry points.

#include "config.hpp"
#include "engine/hooks.hpp"
#include "engine/pose.hpp"
#include "engine/posestream.hpp"
#include "engine/trace.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/protocol.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

#include <windows.h>

#include <filesystem>

namespace {
const RED4ext::v1::Sdk *g_sdk{};

std::filesystem::path PluginDirectory(RED4ext::v1::PluginHandle handle) {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(reinterpret_cast<HMODULE>(handle), path, MAX_PATH);
  return std::filesystem::path(path).parent_path();
}
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
    // A command left from an earlier run is not for this one.
    std::error_code ignored;
    std::filesystem::remove(runtime.config.commandFile, ignored);
    log::Info("role: {}", NameOf(runtime.config.role));

    Red::TypeInfoRegistrar::RegisterDiscovered();
    // Installed later, from the game thread (TandemNative.PoseHook).
    engine::SetPoseHookContext(sdk, handle);
    if (runtime.config.role != Config::Role::Off)
      engine::InstallHooks(sdk, handle);
    if (runtime.config.role != Config::Role::Off && runtime.config.pose)
      engine::InstallPoseStream(sdk, handle);
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
