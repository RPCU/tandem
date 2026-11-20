// Would a device action cast its doer? (casts.hpp; research/engine/story.md
// §13.65, read offline on 2.50.)
//
// Compiled script bytecode, as the game holds it once loaded, names what it
// calls: a final or static call by its function's address (CBaseFunction*),
// a virtual call by the function's short name (a CName, 64 bits). Either is
// an 8-byte value in the function's bytecode buffer
// (CBaseFunction::bytecode, a CCompiledCode at +0x78: its DataBuffer's data
// and size). Reading every 8-byte window of a buffer and matching it against
// the addresses and names we look for finds the calls without decoding the
// opcodes (an unrelated 8 bytes equal to one of a few dozen heap addresses
// or 64-bit name hashes does not happen in practice). Read-only: nothing is
// patched or called.
//
// Layout: the SDK's CBaseFunction and CClass (funcs +0x48, parent +0x10,
// listeners +0x1B0, eventTypeId +0x2C0) as generated for 2.x; checked at
// the first use against functions whose shape is known (Device's
// EnterWorkspot is scripted and has bytecode; WorkspotGameSystem's
// PlayInDevice is native and has none). A mismatch answers Unknown, and the
// script treats Unknown as "casts" (the host does it: nothing of the host's
// V is moved by a guess).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "casts.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/RTTITypes.hpp>
#include <RED4ext/Scripting/Functions.hpp>

#include <cstring>
#include <format>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tandem::plugin::engine {
namespace {
using RED4ext::CBaseFunction;
using RED4ext::CClass;
using RED4ext::CName;

constexpr int kMaxDepth = 5;
constexpr std::size_t kMaxFunctions = 400;
constexpr std::uint32_t kMaxBytecode = 4u << 20;

struct Sinks {
  bool ready{}, ok{};
  std::set<std::uint64_t> names;           // CName hashes: virtual calls
  std::set<std::uintptr_t> functions;      // addresses: final and native calls
  std::map<std::uint64_t, CClass *> extra; // classes whose functions are followed too
};

Sinks &TheSinks() {
  static Sinks sinks;
  return sinks;
}

std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<Casts, std::string>> &Cache() {
  static std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<Casts, std::string>> cache;
  return cache;
}

CClass *ClassOf(std::uint64_t name) {
  if (name == 0)
    return nullptr;
  return RED4ext::CRTTISystem::Get()->GetClass(CName(name));
}

const std::uint8_t *Code(const CBaseFunction *function, std::uint32_t &size) {
  size = 0;
  if (!function || function->flags.isNative)
    return nullptr;
  const auto &buffer = function->bytecode.bytecode.buffer;
  if (!buffer.data || buffer.size == 0 || buffer.size > kMaxBytecode)
    return nullptr;
  size = buffer.size;
  return static_cast<const std::uint8_t *>(buffer.data);
}

// The workspot system's entry points and the device's helpers around them.
void Prepare() {
  auto &sinks = TheSinks();
  if (sinks.ready)
    return;
  sinks.ready = true;
  for (const char *name :
       {"PlayInDevice", "PlayInDeviceSimple", "EnterWorkspot", "EnterWorkspotWithIK"})
    sinks.names.insert(CName(name).hash);
  auto *workspots = RED4ext::CRTTISystem::Get()->GetClass("WorkspotGameSystem");
  auto *device = RED4ext::CRTTISystem::Get()->GetClass("Device");
  auto *operations = RED4ext::CRTTISystem::Get()->GetClass("DeviceOperations");
  if (!workspots || !device) {
    log::Warn("casts: WorkspotGameSystem or Device is not in the RTTI: every interaction counts "
              "as casting its doer");
    return;
  }
  const CBaseFunction *native = nullptr;
  for (const char *name : {"PlayInDevice", "PlayInDeviceSimple"})
    if (const auto *function = workspots->GetFunction(CName(name))) {
      sinks.functions.insert(reinterpret_cast<std::uintptr_t>(function));
      native = function;
    }
  for (const char *name : {"EnterWorkspot", "EnterWorkspotWithIK"})
    if (const auto *function = device->GetFunction(CName(name)))
      sinks.functions.insert(reinterpret_cast<std::uintptr_t>(function));
  if (operations)
    sinks.extra[CName("DeviceOperations").hash] = operations;
  // The shape we expect: a native without bytecode, a scripted one with.
  std::uint32_t size = 0;
  const auto *enter = device->GetFunction(CName("EnterWorkspot"));
  sinks.ok = native && native->flags.isNative && enter && !enter->flags.isNative &&
             Code(enter, size) != nullptr;
  if (sinks.ok)
    log::Info("casts: reading device callbacks (Device.EnterWorkspot: {} bytes of bytecode)", size);
  else
    log::Warn("casts: the functions are not as measured on 2.50: every interaction counts as "
              "casting its doer");
}

struct Index {
  std::unordered_map<std::uint64_t, std::vector<const CBaseFunction *>> byName;
  std::unordered_map<std::uintptr_t, const CBaseFunction *> byAddress;
};

void AddClass(Index &index, const CClass *cls) {
  for (; cls; cls = cls->parent)
    for (const auto *function : cls->funcs) {
      if (!function)
        continue;
      index.byName[function->shortName.hash].push_back(function);
      index.byAddress[reinterpret_cast<std::uintptr_t>(function)] = function;
    }
}

const CClass *HandleInner(const RED4ext::CProperty *param) {
  if (!param || !param->type)
    return nullptr;
  const auto *type = param->type;
  const auto kind = type->GetType();
  if (kind == RED4ext::rtti::ERTTIType::Handle)
    type = static_cast<const RED4ext::CRTTIHandleType *>(type)->innerType;
  else if (kind == RED4ext::rtti::ERTTIType::WeakHandle)
    type = static_cast<const RED4ext::CRTTIWeakHandleType *>(type)->innerType;
  if (!type || type->GetType() != RED4ext::rtti::ERTTIType::Class)
    return nullptr;
  return static_cast<const CClass *>(type);
}

// The callbacks a class (and its parents) runs for an event of class `action`.
void Callbacks(const CClass *cls, const CClass *action, std::vector<const CBaseFunction *> &out) {
  for (const auto *c = cls; c; c = c->parent) {
    // By the listeners the engine dispatches events with.
    for (const auto &listener : c->listeners)
      if (listener.eventTypeId == action->eventTypeId && action->eventTypeId != 0)
        if (const auto *function = c->GetFunction(listener.callbackName))
          out.push_back(function);
    // And by their shape: an event callback taking exactly that class.
    for (const auto *function : c->funcs)
      if (function && function->flags.isEvent && function->params.Size() == 1 &&
          HandleInner(function->params[0]) == action)
        out.push_back(function);
  }
}

bool Scan(const CBaseFunction *root, const Index &index, std::string &why) {
  const auto &sinks = TheSinks();
  std::set<const CBaseFunction *> seen;
  std::vector<std::pair<const CBaseFunction *, std::string>> stack{
      {root, root->shortName.ToString()}};
  std::vector<int> depths{0};
  while (!stack.empty() && seen.size() < kMaxFunctions) {
    auto [function, path] = std::move(stack.back());
    const int depth = depths.back();
    stack.pop_back();
    depths.pop_back();
    if (!seen.insert(function).second)
      continue;
    std::uint32_t size = 0;
    const auto *code = Code(function, size);
    for (std::uint32_t i = 0; code && i + 8 <= size; ++i) {
      std::uint64_t value;
      std::memcpy(&value, code + i, sizeof value);
      if (sinks.names.contains(value) || sinks.functions.contains(value)) {
        why = path;
        return true;
      }
      if (depth >= kMaxDepth)
        continue;
      if (const auto it = index.byAddress.find(value); it != index.byAddress.end()) {
        stack.push_back({it->second, path + " > " + it->second->shortName.ToString()});
        depths.push_back(depth + 1);
      } else if (const auto named = index.byName.find(value); named != index.byName.end()) {
        for (const auto *callee : named->second) {
          stack.push_back({callee, path + " > " + callee->shortName.ToString()});
          depths.push_back(depth + 1);
        }
      }
    }
  }
  return false;
}
} // namespace

Casts ActionCastsDoer(std::uint64_t entityClass, std::uint64_t stateClass,
                      std::uint64_t actionClass, std::string &why) {
  const auto key = std::make_pair(entityClass, actionClass);
  auto &cache = Cache();
  if (const auto it = cache.find(key); it != cache.end()) {
    why = it->second.second;
    return it->second.first;
  }
  Prepare();
  auto answer = Casts::Unknown;
  const auto *entity = ClassOf(entityClass);
  const auto *state = ClassOf(stateClass);
  const auto *action = ClassOf(actionClass);
  if (!TheSinks().ok) {
    why = "layout not as measured";
  } else if (!entity || !action) {
    why = std::format("class {:#x} or {:#x} not in the RTTI", entityClass, actionClass);
  } else {
    Index index;
    AddClass(index, entity);
    AddClass(index, state);
    for (const auto &entry : TheSinks().extra)
      AddClass(index, entry.second);
    std::vector<const CBaseFunction *> roots;
    Callbacks(entity, action, roots);
    Callbacks(state, action, roots);
    // A subclass of the action may be handled by its parent's callback.
    if (roots.empty() && action->parent) {
      Callbacks(entity, action->parent, roots);
      Callbacks(state, action->parent, roots);
    }
    answer = Casts::No;
    why = roots.empty() ? "no callback for it" : std::format("{} callback(s), none", roots.size());
    for (const auto *root : roots)
      if (Scan(root, index, why)) {
        answer = Casts::Yes;
        break;
      }
  }
  log::Info("casts: {} on {}: {} ({})", CName(actionClass).ToString(),
            CName(entityClass).ToString(),
            answer == Casts::Yes  ? "casts its doer"
            : answer == Casts::No ? "does not cast its doer"
                                  : "unknown",
            why);
  cache[key] = {answer, why};
  return answer;
}
} // namespace tandem::plugin::engine
