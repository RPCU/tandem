#include "tandem/degraded.hpp"

#include <algorithm>
#include <format>

namespace tandem::degraded {
namespace {
using enum Class;
using enum Kind;
// In the order they are listed. Engine features first, by their names in
// plugin/src/engine/access.hpp; then the scripts' own. Append; an id that
// went out of use stays (another build may still say it).
constexpr Info kCatalogue[] = {
    // ---- engine access: a hash or a layout that is not this game's ----
    // Core: without these the other players' bodies stand still, or a
    // guest's own quests run beside the host's (degradations.md argues each).
    {"puppet movement", Core, State, "remote players and NPC proxies do not move"},
    {"pose stream", Core, State, "no body pose is read or shown: puppets do not animate"},
    {"quest nodes", Core, State, "no quest node is seen: no freeze, no scene privacy or replay"},
    {"quest freeze", Core, State, "a guest's own quests run beside the host's"},
    {"clip lengths", Degraded, State, "a puppet's gait keeps its default pace"},
    {"looks", Degraded, State, "nobody is dressed: every V is the stand-in body"},
    {"look keys", Degraded, State, "our own look is not read or sent"},
    {"third-person mark", Degraded, State, "a puppet's clothes may stay first-person ones"},
    {"quest prefabs", Degraded, State, "a guest does not see the host's quest interiors"},
    {"workspots", Degraded, State, "an NPC's workspot is not read: it is not shown working"},
    {"physics watch", Degraded, State, "a long physics pause is not watched"},
    {"transform guard", Degraded, State, "the engine's crash on an expired entity is not guarded"},
    {"dismemberment guard", Degraded, State,
     "the engine's crash on an NPC detached in the middle of an action is not guarded"},
    {"slide guard", Degraded, State,
     "the engine's crash on an NPC proxy seated in a vehicle is not guarded"},
    {"dispose gate", Degraded, State,
     "the engine's crash on a car removed while the world detaches is not guarded"},
    {"anim trace", Degraded, State, "no animation trace, no mirror"},
    {"vehicle move", Degraded, State,
     "vehicles other games run are teleported every frame, not moved by the engine"},
    {"body slots", Degraded, State,
     "a puppet's body gets no grapple slot: a grappled NPC is held in front of it"},
    {"crowd spawners", Degraded, State,
     "where another game runs the street, our own traffic and parked cars still spawn there "
     "and are removed out of sight"},
    {"ragdoll state", Degraded, State,
     "bodies are neither read nor laid as their owner's lie: each copy lies as its own fall left "
     "it"},
    {"activity marks", Degraded, State,
     "a grouped guest's map shows its own NCPD activities, not the host's world's"},
    // ---- states the plugin and the scripts find ----
    {"drawn prompts", Degraded, State,
     "prompts on players and vendors are drawn by tandem, not the game's interactions"},
    {"attitude relations", Degraded, State,
     "the declared group relations did not take: attitudes are copied agent by agent"},
    {"scene privacy", Degraded, State, "every scene counts as important: none is private"},
    {"interaction casts", Degraded, State, "every interaction counts as casting its doer"},
    {"scene voice", Degraded, State, "V's voice in a replayed scene is the game's pick"},
    {"nameplate projection", Degraded, State,
     "nameplates are placed by tandem's own projection, not the engine's"},
    // ---- fallbacks: a lesser path taken for one body, once ----
    {"prompt drawn", Degraded, Fallback, "one body's prompt is drawn, not the game's own"},
    {"vehicle teleported", Degraded, Fallback,
     "one vehicle another game runs is teleported, not moved"},
    {"street body respawned", Degraded, Fallback,
     "a street vehicle's body was gone and was spawned again"},
    {"proxy respawned", Degraded, Fallback,
     "an NPC proxy lost its body or never woke up: spawned again"},
    {"npc state polled", Degraded, Fallback,
     "an NPC's change came from the safety poll: its event was missed"},
    {"loot not shared", Degraded, Fallback, "items dropped the game's usual way, not shared"},
    {"record substituted", Degraded, Fallback,
     "another game's record is unknown here: a substitute, or nothing"},
    {"look not received", Degraded, Fallback,
     "a player sent no look: a stand-in body (the mock player's case)"},
    {"stand-in look", Degraded, Fallback, "a player's look could not be put on: a stand-in body"},
    {"partner call", Degraded, Fallback,
     "the call of a partner could not be placed on the phone: their request is a notice and hints "
     "only"},
    {"wanted stars", Degraded, Fallback,
     "a wanted partner's stars are written as a number, not drawn in the game's art"},
    {"corpse pose", Degraded, Fallback,
     "a body's ragdoll was not read or laid: its copy lies as its own ragdoll left it"},
    {"body incomplete", Degraded, Fallback,
     "a puppet's body misses a part of its donor's (it glides, or NPCs do not see it)"},
    {"workspot not entered", Degraded, Fallback, "an NPC could not enter its workspot: it stands"},
    {"workspot unread", Degraded, Fallback,
     "an NPC's workspot could not be read: its copies stand where it sits"},
    {"scene camera missing", Degraded, Fallback,
     "a scene's camera body has no camera: our own eyes stay"},
    {"carried by placement", Degraded, Fallback,
     "no trunk body slot to carry our V: placed in the car a few times a second"},
    {"revive ended by hand", Degraded, Fallback, "a revived V's death effects were ended by hand"},
    {"race watched", Degraded, Fallback, "our V could not get into the race car: we watch"},
    {"v slowed as entity", Degraded, Fallback,
     "our V is slowed as an entity, not by the player's own time factor"},
    {"radio not matched", Degraded, Fallback, "a car's radio would not take its driver's station"},
    {"device not matched", Degraded, Fallback, "a device would not become what the host has"},
    {"police not spawned", Degraded, Fallback, "a police unit could not spawn"},
    {"quest signals dropped", Degraded, Fallback,
     "signals held for a guest's frozen story were dropped"},
    {"no room", Degraded, Fallback, "no slot left for another puppet, pose or rig"},
    {"puppet not driven", Degraded, Fallback, "a puppet has no move component to drive"},
    {"hit shown by twitch", Degraded, Fallback,
     "a hit a V took is shown as the body's twitch: the game's pipeline played nothing"},
    {"summoned car unseen", Degraded, Fallback,
     "a partner's summoned car has no copy here: it appears when they get in"},
    {"activity not marked", Degraded, Fallback,
     "one NCPD activity's map pin would not take the host's state: it shows our own"},
    {"activity untracked", Degraded, Fallback,
     "an NCPD activity the player tracked, shown done during the session, is not tracked again"},
};

std::size_t OrderOf(std::string_view id) {
  for (std::size_t i = 0; i < std::size(kCatalogue); ++i)
    if (kCatalogue[i].id == id)
      return i;
  return std::size(kCatalogue);
}

void Sort(std::vector<Entry> &entries) {
  std::ranges::stable_sort(entries, [](const Entry &a, const Entry &b) {
    const auto x = OrderOf(a.id), y = OrderOf(b.id);
    return x != y ? x < y : a.id < b.id;
  });
}

Entry *Of(std::vector<Entry> &entries, std::string_view id) {
  for (auto &entry : entries)
    if (entry.id == id)
      return &entry;
  return nullptr;
}

Entry Make(std::string_view id, Kind kind, std::string_view reason, bool chosen) {
  const auto *info = Find(id);
  return {.id = std::string(id),
          .klass = info ? info->klass : Degraded,
          .kind = kind,
          .chosen = chosen,
          .known = info != nullptr && info->kind == kind,
          .reason = std::string(reason),
          .count = 1};
}

std::string Tags(const Entry &entry) {
  std::string tags = entry.klass == Core ? "core" : "degraded";
  if (entry.chosen)
    tags += ", chosen";
  if (!entry.known)
    tags += ", unknown id";
  return tags;
}
} // namespace

std::span<const Info> Catalogue() { return kCatalogue; }

const Info *Find(std::string_view id) {
  for (const auto &info : kCatalogue)
    if (info.id == id)
      return &info;
  return nullptr;
}

bool Registry::Start(std::string_view id, std::string_view reason, bool chosen) {
  std::lock_guard _(m_lock);
  if (auto *held = Of(m_states, id)) {
    // Really off beats switched off: the reason that is no choice is kept.
    if (!held->chosen || chosen)
      return false;
    held->chosen = false;
    held->reason = std::string(reason);
    ++m_changes;
    return true;
  }
  m_states.push_back(Make(id, State, reason, chosen));
  ++m_changes;
  return true;
}

bool Registry::End(std::string_view id) {
  std::lock_guard _(m_lock);
  const auto erased = std::erase_if(m_states, [&](const Entry &entry) { return entry.id == id; });
  if (erased)
    ++m_changes;
  return erased != 0;
}

std::uint32_t Registry::Count(std::string_view id, std::string_view reason) {
  std::lock_guard _(m_lock);
  ++m_changes;
  if (auto *taken = Of(m_fallbacks, id)) {
    taken->reason = std::string(reason);
    return ++taken->count;
  }
  m_fallbacks.push_back(Make(id, Fallback, reason, false));
  return 1;
}

void Registry::ResetFallbacks() {
  std::lock_guard _(m_lock);
  if (!m_fallbacks.empty())
    ++m_changes;
  m_fallbacks.clear();
}

std::vector<Entry> Registry::ListLocked() const {
  auto states = m_states, fallbacks = m_fallbacks;
  Sort(states);
  Sort(fallbacks);
  states.insert(states.end(), fallbacks.begin(), fallbacks.end());
  return states;
}

std::vector<Entry> Registry::List() const {
  std::lock_guard _(m_lock);
  return ListLocked();
}

std::vector<Entry> Registry::CoreOff() const {
  std::lock_guard _(m_lock);
  std::vector<Entry> out;
  for (const auto &entry : m_states)
    if (entry.klass == Core && !entry.chosen)
      out.push_back(entry);
  Sort(out);
  return out;
}

std::uint64_t Registry::Changes() const {
  std::lock_guard _(m_lock);
  return m_changes;
}

proto::Degraded Registry::Wire() const {
  std::lock_guard _(m_lock);
  proto::Degraded out;
  for (const auto &entry : m_states) {
    auto id = WireId(entry.id);
    if (id.empty() || std::ranges::any_of(out.entries, [&](const proto::DegradedEntry &said) {
          return said.id == id;
        }))
      continue;
    out.entries.push_back(
        {.id = std::move(id),
         .flags = static_cast<std::uint8_t>((entry.chosen ? proto::kDegradedChosen : 0) |
                                            (entry.klass == Core ? proto::kDegradedCore : 0))});
  }
  std::ranges::sort(out.entries, {}, &proto::DegradedEntry::id);
  if (out.entries.size() > proto::kMaxDegraded)
    out.entries.resize(proto::kMaxDegraded);
  return out;
}

std::string Registry::Summary() const {
  std::lock_guard _(m_lock);
  std::string states, fallbacks;
  for (const auto &entry : ListLocked()) {
    auto &list = entry.kind == State ? states : fallbacks;
    list += list.empty() ? "" : ", ";
    list += entry.id;
    if (entry.kind == Fallback)
      list += std::format(" x{}", entry.count);
    else if (entry.klass == Core || entry.chosen)
      list += std::format(" ({}{}{})", entry.klass == Core ? "core" : "",
                          entry.klass == Core && entry.chosen ? ", " : "",
                          entry.chosen ? "chosen" : "");
  }
  if (states.empty() && fallbacks.empty())
    return "degraded: none";
  return std::format("degraded: {}{}{}", states, !states.empty() && !fallbacks.empty() ? "; " : "",
                     fallbacks.empty() ? "" : "fallbacks: " + fallbacks);
}

std::vector<std::string> Registry::Report() const {
  std::lock_guard _(m_lock);
  const auto entries = ListLocked();
  std::size_t core = 0, lesser = 0, chosen = 0, fallbacks = 0, times = 0;
  for (const auto &entry : entries) {
    if (entry.kind == Fallback) {
      ++fallbacks;
      times += entry.count;
    } else if (entry.chosen) {
      ++chosen;
    } else if (entry.klass == Core) {
      ++core;
    } else {
      ++lesser;
    }
  }
  std::vector<std::string> lines;
  lines.push_back(
      std::format("degraded: registry: {} core, {} degraded, {} chosen, {} fallbacks ({} times)",
                  core, lesser, chosen, fallbacks, times));
  for (const auto &entry : entries)
    lines.push_back(LineOf(entry));
  lines.push_back("degraded: registry end");
  return lines;
}

std::string CoreProblem(const Entry &entry) {
  const auto *info = Find(entry.id);
  return std::format("{}{} is off ({}){}. tandem does not match this game version: update "
                     "tandem, or the game. Co-op is off.",
                     kCoreProblem, entry.id, entry.reason,
                     info ? std::format(", and without it {}", info->means) : "");
}

std::string LineOf(const Entry &entry) {
  if (entry.kind == Fallback)
    return std::format("degraded: fallback {} [{}] x{}: {}", entry.id, Tags(entry), entry.count,
                       entry.reason);
  return std::format("degraded: state {} [{}]: {}", entry.id, Tags(entry), entry.reason);
}

std::string Describe(const proto::Degraded &said) {
  std::string out;
  for (const auto &entry : said.entries) {
    out += out.empty() ? "" : ", ";
    out += entry.id;
    const bool core = entry.flags & proto::kDegradedCore;
    const bool chosen = entry.flags & proto::kDegradedChosen;
    if (core || chosen)
      out += std::format(" ({}{}{})", core ? "core" : "", core && chosen ? ", " : "",
                         chosen ? "chosen" : "");
  }
  return out.empty() ? "nothing" : out;
}

std::string WireId(std::string_view id) {
  std::string out;
  for (const char c : id.substr(0, proto::kMaxDegradedId)) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-')
      out += c;
    else if (c >= 'A' && c <= 'Z')
      out += static_cast<char>(c - 'A' + 'a');
    else
      out += '-';
  }
  return out;
}
} // namespace tandem::degraded
