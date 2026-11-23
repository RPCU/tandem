// Scene privacy (engine-knowledge.md §13.23, read offline on 2.50).
//
// A quest starts a scene through a questSceneNodeDefinition (RTTI size
// 0x88; `sceneFile` at +0x48, a resource path) run by
// QuestPhaseInstance::ExecuteNode; the engine wakes the node again and again
// while the scene plays, so "the scene node seen last" is the scene now.
// The scene's resource, scnSceneResource (RTTI size 0x2D8), says what kind
// of scene it is: `sceneCategoryTag` (scnSceneCategoryTag: mainQuests,
// sideQuests, minorQuests, dialoguesQuests, chatsOpenWorld, holocalls…) and
// who plays in it (`actors`, each with an `actorName` and a character
// record; `playerActors`). The scene system has it loaded while it plays:
// the resource loader's token for its path holds it
// (ResourceLoader::FindToken, address hash 0xC86F39B7 =
// TryAcquireResourceToken_NoLock, under the loader's token lock).
//
// Field offsets come from the RTTI by name (properties' value offsets), not
// by hand; a missing class or property turns classification off (every
// scene counts as Important: nothing is hidden by mistake).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../degradations.hpp"
#include "../log.hpp"
#include "../observer.hpp"
#include "scenes.hpp"

#include "tandem/session.hpp"

#include <RED4ext/CString.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/ResourceLoader.hpp>
#include <RED4ext/Scripting/Natives/CResource.hpp>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <map>
#include <mutex>

namespace tandem::plugin::engine {
namespace {
std::atomic<std::uint64_t> g_lastPath{0};
std::atomic<std::uint64_t> g_lastUs{0};
std::atomic<RED4ext::CClass *> g_nodeClass{nullptr};
std::atomic_bool g_layoutChecked{false}, g_layoutOk{false};
// By RTTI: questSceneNodeDefinition.sceneFile, scnSceneResource
// .sceneCategoryTag / .actors, scnActorDef.actorName, scnActorDef's size.
std::atomic<std::uint32_t> g_sceneFile{0};
std::uint32_t g_category = 0, g_actors = 0, g_actorName = 0, g_actorSize = 0;

// scnSceneCategoryTag (the SDK's generated enum, the game's values).
enum class Tag : std::int8_t {
  voiceset = 0,
  mainQuests = 1,
  sideQuests = 2,
  minorQuests = 3,
  otherQuests = 4,
  dialoguesQuests = 5,
  streetOpenWorld = 6,
  vendorsOpenWorld = 7,
  dancefloorsOpenWorld = 8,
  cityOpenWorld = 9,
  chatsOpenWorld = 10,
  otherOpenWorld = 11,
  holocalls = 12,
  other = 13,
};

ScenePrivacy g_privacy;                         // set at load
std::vector<std::uint64_t> g_private, g_public; // their path hashes
std::map<std::uint64_t, SceneClass> g_cache;    // game thread

std::string Lower(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}

bool LayoutOk() {
  if (!g_layoutChecked.exchange(true)) {
    auto *rtti = RED4ext::CRTTISystem::Get();
    auto *node = rtti->GetClass("questSceneNodeDefinition");
    auto *scene = rtti->GetClass("scnSceneResource");
    auto *actor = rtti->GetClass("scnActorDef");
    const auto offset = [](RED4ext::CClass *type, const char *name) -> std::uint32_t {
      const auto *property = type ? type->GetProperty(name) : nullptr;
      return property ? property->valueOffset : 0;
    };
    const auto sceneFile = offset(node, "sceneFile");
    g_category = offset(scene, "sceneCategoryTag");
    g_actors = offset(scene, "actors");
    g_actorName = offset(actor, "actorName");
    g_actorSize = actor ? actor->GetSize() : 0;
    const bool ok =
        sceneFile && g_category && g_actors && g_actorName && g_actorSize >= g_actorName + 8;
    g_layoutOk = ok;
    if (ok) {
      g_sceneFile = sceneFile;
      g_nodeClass = node;
      log::Info(
          "scenes: sceneFile +{:#x}, category +{:#x}, actors +{:#x} (each {:#x}, name +{:#x})",
          sceneFile, g_category, g_actors, g_actorSize, g_actorName);
    } else {
      log::Error("scenes: questSceneNodeDefinition, scnSceneResource or scnActorDef differ "
                 "from 2.50: every scene counts as important (none private)");
      Degradations::Get().Start("scene privacy",
                                "the scene classes differ from 2.50: every scene counts as "
                                "important (none private)");
    }
  }
  return g_layoutOk;
}

const char *Category(Tag tag) {
  using T = Tag;
  switch (tag) {
  case T::voiceset:
    return "voiceset";
  case T::mainQuests:
    return "mainQuests";
  case T::sideQuests:
    return "sideQuests";
  case T::minorQuests:
    return "minorQuests";
  case T::otherQuests:
    return "otherQuests";
  case T::dialoguesQuests:
    return "dialoguesQuests";
  case T::streetOpenWorld:
    return "streetOpenWorld";
  case T::vendorsOpenWorld:
    return "vendorsOpenWorld";
  case T::dancefloorsOpenWorld:
    return "dancefloorsOpenWorld";
  case T::cityOpenWorld:
    return "cityOpenWorld";
  case T::chatsOpenWorld:
    return "chatsOpenWorld";
  case T::otherOpenWorld:
    return "otherOpenWorld";
  case T::holocalls:
    return "holocalls";
  case T::other:
    return "other";
  }
  return "?";
}

// ---- guarded reads (no C++ objects with destructors in __try functions) ----
// The observer runs in the quest system for every node of every game, and
// the scene resource is read while the engine owns it: a bad pointer is
// caught here and the scene counts as important (never private by mistake).

constexpr std::uint32_t kMaxActors = 32, kNameBytes = 64;

struct SceneFacts {
  std::int8_t category = -1;
  std::uint32_t actors = 0;
  char names[kMaxActors][kNameBytes] = {};
};

// The node is a scene node: its scene's resource path (0: not one, or unreadable).
std::uint64_t ScenePathOf(void *node, RED4ext::CClass *wanted, std::uint32_t offset) {
  __try {
    auto *type = static_cast<RED4ext::ISerializable *>(node)->GetType();
    if (!type || !type->IsA(wanted))
      return 0;
    return *reinterpret_cast<const std::uint64_t *>(static_cast<const std::uint8_t *>(node) +
                                                    offset);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

bool ReadScene(const std::uint8_t *scene, SceneFacts &facts) {
  __try {
    facts.category = *reinterpret_cast<const std::int8_t *>(scene + g_category);
    // DynArray's head: entries, capacity, size (the elements are scnActorDef).
    const auto *head = scene + g_actors;
    const auto *entries = *reinterpret_cast<const std::uint8_t *const *>(head);
    facts.actors = *reinterpret_cast<const std::uint32_t *>(head + 12);
    if (facts.actors > 256)
      return false;
    for (std::uint32_t i = 0; i < facts.actors && i < kMaxActors && entries; ++i) {
      const auto *name =
          reinterpret_cast<const RED4ext::CString *>(entries + i * g_actorSize + g_actorName)
              ->c_str();
      for (std::uint32_t c = 0; name && c + 1 < kNameBytes && name[c]; ++c)
        facts.names[i][c] = name[c];
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// The rules (design §2.12). A scene of the story (main quests, a side
// quest's scenes with several people, holocalls) is for everyone; chats,
// open-world and minor one-to-one scenes, and a side or minor quest's scene
// with a romance partner alone, are private.
SceneClass Classify(const SceneFacts &facts, std::string &why) {
  using T = Tag;
  if (facts.category < 0 || facts.category > 13) {
    why = std::format("unknown category {}", facts.category);
    return SceneClass::Important;
  }
  const auto category = static_cast<Tag>(facts.category);
  const std::uint32_t npcs = facts.actors;
  std::string partner;
  for (std::uint32_t i = 0; i < npcs && i < kMaxActors; ++i) {
    const auto name = Lower(facts.names[i]);
    for (const auto &wanted : g_privacy.actors)
      if (!wanted.empty() && name.find(wanted) != std::string::npos)
        partner = name;
  }
  why = std::format("{}, {} actor(s){}", Category(category), npcs,
                    partner.empty() ? "" : std::format(", partner {}", partner));
  switch (category) {
  case T::voiceset:
  case T::dialoguesQuests:
  case T::streetOpenWorld:
  case T::vendorsOpenWorld:
  case T::dancefloorsOpenWorld:
  case T::cityOpenWorld:
  case T::chatsOpenWorld:
  case T::otherOpenWorld:
    return SceneClass::Minor;
  case T::sideQuests:
  case T::minorQuests:
  case T::otherQuests:
    if (!partner.empty() && npcs <= 2)
      return SceneClass::Romance;
    if (category == T::minorQuests && npcs <= 1)
      return SceneClass::Minor;
    return SceneClass::Important;
  case T::mainQuests:
  case T::holocalls:
  case T::other:
    return SceneClass::Important;
  }
  return SceneClass::Important;
}
} // namespace

void SetScenePrivacy(const ScenePrivacy &privacy) {
  g_privacy = privacy;
  for (auto &actor : g_privacy.actors)
    actor = Lower(actor);
  for (const auto &path : privacy.privates)
    g_private.push_back(RED4ext::ResourcePath(path.c_str()).hash);
  for (const auto &path : privacy.publics)
    g_public.push_back(RED4ext::ResourcePath(path.c_str()).hash);
}

void ObserveSceneNode(void *node) {
  auto *wanted = g_nodeClass.load(std::memory_order_relaxed);
  if (!wanted || !node)
    return;
  // RaRef<scnSceneResource>: its resource path first.
  const auto path = ScenePathOf(node, wanted, g_sceneFile.load());
  if (path == 0)
    return;
  g_lastPath.store(path, std::memory_order_relaxed);
  g_lastUs.store(net::NowUs(), std::memory_order_relaxed);
}

std::uint64_t RecentScenePath() { return g_lastPath.load(); }

std::uint64_t ScenePlaying(std::uint32_t maxAgeMs) {
  // The layout check also arms the observer (it needs the node class).
  if (!LayoutOk())
    return 0;
  const auto path = g_lastPath.load();
  const auto at = g_lastUs.load();
  if (path == 0 || net::NowUs() - at > static_cast<std::uint64_t>(maxAgeMs) * 1000)
    return 0;
  return path;
}

SceneClass ClassifyRecentScene(std::uint32_t maxAgeMs, std::string &why) {
  // The layout check also arms the observer (it needs the node class).
  if (!LayoutOk())
    return SceneClass::None;
  const auto path = g_lastPath.load();
  const auto at = g_lastUs.load();
  if (path == 0 || net::NowUs() - at > static_cast<std::uint64_t>(maxAgeMs) * 1000)
    return SceneClass::None;
  if (std::ranges::find(g_public, path) != g_public.end()) {
    why = "listed public";
    return SceneClass::Important;
  }
  if (std::ranges::find(g_private, path) != g_private.end()) {
    why = "listed private";
    return SceneClass::Minor;
  }
  if (const auto it = g_cache.find(path); it != g_cache.end()) {
    why = "known";
    return it->second;
  }
  auto *loader = RED4ext::ResourceLoader::Get();
  if (!loader)
    return SceneClass::None;
  auto token = loader->FindToken<RED4ext::CResource>(RED4ext::ResourcePath(path));
  if (!token || !token->IsLoaded() || !token->resource)
    return SceneClass::None; // not loaded yet: asked again next time
  auto *resource = token->resource.GetPtr();
  static auto *sceneClass = RED4ext::CRTTISystem::Get()->GetClass("scnSceneResource");
  if (!resource || !sceneClass || resource->GetType() != sceneClass)
    return SceneClass::None;
  SceneFacts facts;
  SceneClass result = SceneClass::Important;
  if (ReadScene(reinterpret_cast<const std::uint8_t *>(resource), facts))
    result = Classify(facts, why);
  else
    why = "unreadable";
  g_cache[path] = result;
  const char *kind = result == SceneClass::Romance ? "private (romance)"
                     : result == SceneClass::Minor ? "private (minor)"
                                                   : "important";
  log::Info("scenes: {:#x} is {} ({})", path, kind, why);
  if (Observer::On()) {
    std::string actors;
    for (std::uint32_t i = 0; i < facts.actors && i < kMaxActors && i < 8; ++i)
      actors += std::format("{}{}", i ? ", " : "", facts.names[i]);
    Observer::Get().Note("scene-kind", std::format("{:#x} {} ({}){}", path, kind, why,
                                                   actors.empty() ? "" : ": " + actors));
  }
  return result;
}
} // namespace tandem::plugin::engine
