#pragma once
// Guards around engine crashes that are not ours to fix: see guards.cpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>

#include <string>

namespace tandem::plugin::engine {
// At plugin load, under Wine (Proton) only: the transform update's guard
// against an expired entity (docs/research/engine/re.md §13.98). Its state
// for the startup line: "on", or "off (why)", the reason also logged.
std::string InstallCrashGuards(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
// At plugin load, on every platform: the guard of a dismemberment component
// told of its entity's mask change after its own detach (re.md §13.142).
// Only installed when the function's start is the one measured on 2.50; it
// passes through until CheckDismembermentGuard (CheckAccess, the first
// frame) has found the class's size. For the startup line: "hooked (…)" or
// "off (why)".
std::string InstallDismembermentGuard(const RED4ext::v1::Sdk *sdk,
                                      RED4ext::v1::PluginHandle handle);
void CheckDismembermentGuard();
// At plugin load, on every platform: the guard of the SlideToSimple move
// controller handed an entity that is not a puppet (engine/npcs.md
// §13.154). A pass-through until CheckSlideGuard (the first frame) has
// gamePuppet's class. For the startup line: "hooked (…)" or "off (why)".
std::string InstallSlideGuard(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
void CheckSlideGuard();
// Game thread, every frame: what the guards skipped on job threads, said
// when it changed and at most every 600 frames
// (`dismemberment guard: N detached notifications skipped`).
void ReportCrashGuards();
void RemoveCrashGuards(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
} // namespace tandem::plugin::engine
