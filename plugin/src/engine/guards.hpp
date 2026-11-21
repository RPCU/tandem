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
void RemoveCrashGuards(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
} // namespace tandem::plugin::engine
