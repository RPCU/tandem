// The observer's platform-independent parts: see tandem/observer.hpp.

#include "tandem/observer.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <utility>

namespace tandem::observer {
namespace {
constexpr std::array<const char *, kTriggers> kNames = {
    "stall", "degraded",     "fallback", "respawn", "one-sided",
    "error", "script-error", "drift",    "frame",   "manual",
};

bool Starts(std::string_view text, std::string_view prefix) { return text.starts_with(prefix); }
bool Has(std::string_view text, std::string_view part) {
  return text.find(part) != std::string_view::npos;
}

// The id of a "degraded: fallback ID: REASON" or "… ID xN: REASON" line.
std::string FallbackId(std::string_view rest) {
  const auto colon = rest.find(':');
  auto id = rest.substr(0, colon);
  if (const auto times = id.rfind(" x");
      times != std::string_view::npos && times + 2 < id.size() &&
      std::all_of(id.begin() + static_cast<std::ptrdiff_t>(times) + 2, id.end(),
                  [](char c) { return c >= '0' && c <= '9'; }))
    id = id.substr(0, times);
  return std::string(id);
}

std::string Hex(std::uint64_t value) { return std::format("{:#x}", value); }

// "a, b, c" of up to `max` of `items`, "+N more" after.
template <class T, class F>
std::string Listed(const std::vector<T> &items, std::size_t max, F &&each) {
  std::string out;
  for (std::size_t i = 0; i < items.size() && i < max; ++i)
    out += (i ? ", " : "") + each(items[i]);
  if (items.size() > max)
    out += std::format(" +{} more", items.size() - max);
  return out.empty() ? "none" : out;
}

// What `after` adds to `before` and takes from it (both sorted copies).
std::string Diff(std::vector<std::uint64_t> before, std::vector<std::uint64_t> after) {
  std::ranges::sort(before);
  std::ranges::sort(after);
  std::vector<std::uint64_t> added, removed;
  std::ranges::set_difference(after, before, std::back_inserter(added));
  std::ranges::set_difference(before, after, std::back_inserter(removed));
  std::string out = std::format("{} now", after.size());
  if (!added.empty())
    out += " +" + Listed(added, 6, Hex);
  if (!removed.empty())
    out += " -" + Listed(removed, 6, Hex);
  return out;
}

std::string StoryFlags(std::uint8_t flags) {
  constexpr std::pair<std::uint8_t, const char *> kFlags[] = {
      {proto::kStoryBraindance, "braindance"}, {proto::kStoryReplacer, "replacer"},
      {proto::kStoryInCall, "call"},           {proto::kStoryPrivate, "private"},
      {proto::kStoryMoment, "moment"},         {proto::kStorySpectating, "spectating"},
      {proto::kStoryWatch, "watch"},
  };
  std::string out;
  for (const auto &[bit, name] : kFlags)
    if (flags & bit)
      out += (out.empty() ? "" : "+") + std::string(name);
  return out.empty() ? "none" : out;
}

std::string Gates(std::uint8_t flags) {
  return std::format("Watson lockdown {}, Dogtown {}, in Watson {}, in Dogtown {}",
                     flags & proto::kGateWatsonOpen ? "lifted" : "on",
                     flags & proto::kGateDogtownOpen ? "open" : "shut",
                     (flags & proto::kGateInWatson) != 0, (flags & proto::kGateInDogtown) != 0);
}

std::string Who(proto::PlayerId from) {
  return from == proto::kNoPlayer ? std::string("ours") : std::format("player {}", from);
}
} // namespace

const char *NameOf(Trigger kind) {
  const auto i = static_cast<std::size_t>(kind);
  return i < kNames.size() ? kNames[i] : "?";
}

std::optional<Trigger> TriggerNamed(std::string_view name) {
  for (std::size_t i = 0; i < kNames.size(); ++i)
    if (name == kNames[i])
      return static_cast<Trigger>(i);
  return std::nullopt;
}

double DriftSeverity(float metres) { return 30.0 + std::min(60.0, static_cast<double>(metres)); }
double FrameSeverity(float ms) { return 20.0 + std::min(60.0, static_cast<double>(ms) / 10.0); }

std::optional<Hit> Classify(char level, std::string_view line) {
  const auto hit = [&](Trigger kind, double severity) {
    return Hit{.kind = kind, .detail = std::string(line), .severity = severity};
  };
  if (Starts(line, "observer:"))
    return std::nullopt;
  if (Starts(line, "degraded: fallback ")) {
    const auto id = FallbackId(line.substr(19));
    if (id == "proxy respawned" || id == "street body respawned")
      return hit(Trigger::Respawn, 40);
    return hit(Trigger::Fallback, 30);
  }
  if (Starts(line, "degraded: ")) {
    if (Has(line, " is OFF, co-op cannot run without it"))
      return hit(Trigger::Degraded, 85);
    if (Has(line, " is off by choice") || Has(line, " is back"))
      return std::nullopt;
    if (Has(line, " is off: ") || Has(line, " is not a "))
      return hit(Trigger::Degraded, 55);
    if (Starts(line, "degraded: player ") && Has(line, "'s game runs without: "))
      return hit(Trigger::Degraded, 45);
    return std::nullopt;
  }
  // A copy one game had and the other not, found and put right.
  if (Starts(line, "resync: asking") || Starts(line, "[script] consistency: ") ||
      (Starts(line, "[script] street: check: ") && Has(line, "no longer shared")))
    return hit(Trigger::OneSided, 40);
  if (level == 'E')
    return hit(Starts(line, "[script]") ? Trigger::ScriptError : Trigger::Error, 60);
  if (Starts(line, "[script] ") && (Has(line, " failed") || Has(line, "error")))
    return hit(Trigger::ScriptError, 35);
  return std::nullopt;
}

// ---- Ring ----

Ring::Ring(std::uint64_t keepUs, std::size_t maxBytes) : m_keepUs(keepUs), m_maxBytes(maxBytes) {}

void Ring::Push(std::uint64_t us, char level, std::string text) {
  m_bytes += text.size();
  m_lines.push_back({.us = us, .level = level, .text = std::move(text)});
  while (!m_lines.empty()) {
    const auto &front = m_lines.front();
    const bool old = front.us + m_keepUs < us;
    const bool over = m_bytes > m_maxBytes && m_lines.size() > 1;
    if (!old && !over)
      break;
    if (!old)
      ++m_squeezed;
    m_bytes -= front.text.size();
    m_lines.pop_front();
  }
}

std::vector<Line> Ring::Between(std::uint64_t fromUs, std::uint64_t toUs) const {
  std::vector<Line> out;
  for (const auto &line : m_lines)
    if (line.us >= fromUs && line.us <= toUs)
      out.push_back(line);
  return out;
}

// ---- Limiter ----

Limiter::Limiter(std::uint64_t gapUs, std::uint32_t cap) : m_gapUs(gapUs), m_cap(cap) {}

Limiter::Verdict Limiter::Ask(Trigger kind, std::uint64_t nowUs) {
  if (const auto it = m_last.find(kind); it != m_last.end() && nowUs < it->second + m_gapUs)
    return Verdict::TooSoon;
  if (m_taken >= m_cap)
    return Verdict::Capped;
  m_last[kind] = nowUs;
  ++m_taken;
  return Verdict::Capture;
}

// ---- Trail ----

std::vector<Row> Trail::Note(const proto::Message &message, std::uint64_t us,
                             proto::PlayerId from) {
  std::vector<Row> rows;
  const auto row = [&](std::string kind, std::string text) {
    rows.push_back({.us = us, .kind = std::move(kind), .text = std::move(text)});
  };
  if (const auto *journal = std::get_if<proto::Journal>(&message)) {
    if (journal->snapshot)
      row("journal", std::format("snapshot of {} entries ({})", journal->entries.size(),
                                 from == proto::kNoPlayer ? "ours" : "heard"));
    else if (!journal->entries.empty())
      row("journal", Listed(journal->entries, 8, [](const proto::JournalChange &change) {
            return std::format("{:#x} {}", change.hash, JournalStateName(change.state));
          }));
    for (const auto &change : journal->entries)
      // An entry (quest, objective) that became active: what the run went through.
      if (!journal->snapshot && change.state == 2 &&
          std::ranges::find(m_quests, change.hash) == m_quests.end() && m_quests.size() < 4096)
        m_quests.push_back(change.hash);
  } else if (const auto *tracked = std::get_if<proto::Tracked>(&message)) {
    if (tracked->hash != m_tracked) {
      m_tracked = tracked->hash;
      row("tracked", tracked->hash ? Hex(tracked->hash) : std::string("none"));
    }
  } else if (const auto *scene = std::get_if<proto::SceneState>(&message)) {
    if (scene->scene != m_scene || scene->play != m_play) {
      if (scene->scene == 0)
        row("scene", std::format("end of play {}{}", m_play,
                                 scene->flags & proto::kSceneInterrupted ? " (interrupted)" : ""));
      else {
        row("scene", std::format("start {} play {} tier {}{}", Hex(scene->scene), scene->play,
                                 scene->tier, scene->flags & proto::kSceneStaged ? " staged" : ""));
        if (std::ranges::find(m_scenes, scene->scene) == m_scenes.end() && m_scenes.size() < 4096)
          m_scenes.push_back(scene->scene);
      }
      m_scene = scene->scene;
      m_play = scene->play;
      m_cast.clear();
    }
  } else if (const auto *cast = std::get_if<proto::SceneCast>(&message)) {
    if (cast->actors != m_cast) {
      m_cast = cast->actors;
      row("cast",
          std::format("play {}: {}", cast->play,
                      Listed(cast->actors, 12, [](const proto::SceneActor &actor) {
                        return std::format("npc {}{}", actor.npc,
                                           actor.flags & proto::kActorGhost ? " (ghost)" : "");
                      })));
    }
  } else if (const auto *state = std::get_if<proto::StoryState>(&message)) {
    const auto player = from == proto::kNoPlayer ? proto::kNoPlayer : state->player;
    auto &per = m_story[player];
    const auto who = Who(player);
    if (!per.stated || per.tier != state->tier)
      row("tier", std::format("{}: tier {}", who, state->tier));
    constexpr auto kNoTravel = proto::kStoryNoFastTravel;
    if ((!per.stated && (state->flags & kNoTravel)) ||
        (per.stated && ((per.flags ^ state->flags) & kNoTravel)))
      row("fast-travel", std::format("{}: {}", who, state->flags & kNoTravel ? "locked" : "open"));
    if ((!per.stated && (state->flags & ~kNoTravel)) ||
        (per.stated && ((per.flags ^ state->flags) & ~kNoTravel)))
      row("story-flags",
          std::format("{}: {}", who,
                      StoryFlags(static_cast<std::uint8_t>(state->flags & ~kNoTravel))));
    per = {.tier = state->tier, .flags = state->flags, .stated = true};
  } else if (const auto *restrictions = std::get_if<proto::StoryRestrictions>(&message)) {
    if (restrictions->effects != m_restrictions) {
      row("restrictions", Diff(m_restrictions, restrictions->effects));
      m_restrictions = restrictions->effects;
    }
  } else if (const auto *prefabs = std::get_if<proto::WorldPrefabs>(&message)) {
    if (prefabs->prefabs != m_prefabs) {
      row("prefabs", Diff(m_prefabs, prefabs->prefabs));
      m_prefabs = prefabs->prefabs;
    }
  } else if (const auto *gates = std::get_if<proto::StoryGates>(&message)) {
    // Ours only: the others' are theirs (a guest's gates say where it is).
    if (from == proto::kNoPlayer && gates->flags != m_gates) {
      m_gates = gates->flags;
      row("gates", Gates(gates->flags));
    } else if (from != proto::kNoPlayer)
      row("gates", std::format("player {}: {}", gates->player, Gates(gates->flags)));
  } else if (const auto *call = std::get_if<proto::PhoneCall>(&message)) {
    constexpr const char *kPhases[] = {"?", "incoming", "started", "ended"};
    row("call", std::format("contact {} {}", Hex(call->contact),
                            call->phase < 4 ? kPhases[call->phase] : "?"));
  } else if (const auto *reward = std::get_if<proto::QuestReward>(&message)) {
    row("reward", std::format("{} for quest {}", Hex(reward->reward), Hex(reward->quest)));
  } else if (const auto *effect = std::get_if<proto::StoryEffect>(&message)) {
    row("effect", std::format("{} {}", Hex(effect->effect), effect->applied ? "on" : "off"));
  } else if (const auto *unlock = std::get_if<proto::VehicleUnlock>(&message)) {
    row("reward", std::format("vehicle {} for quest {}", Hex(unlock->vehicle), Hex(unlock->quest)));
  }
  return rows;
}

std::optional<Row> Trail::Phase(std::uint64_t resource, std::uint64_t us) {
  if (resource == 0 || resource == m_lastPhase)
    return std::nullopt;
  m_lastPhase = resource;
  if (m_phases.size() >= 16384 || !m_phases.insert(resource).second)
    return std::nullopt;
  return Row{.us = us, .kind = "phase", .text = std::format("first run of {}", Hex(resource))};
}

std::optional<Row> Trail::District(std::string_view name, std::uint64_t us) {
  if (name == m_district)
    return std::nullopt;
  const auto before = std::exchange(m_district, std::string(name));
  if (!name.empty() && std::ranges::find(m_districts, m_district) == m_districts.end() &&
      m_districts.size() < 256)
    m_districts.push_back(m_district);
  return Row{.us = us,
             .kind = "district",
             .text = std::format("{} (from {})", name.empty() ? "none" : name,
                                 before.empty() ? "none" : before)};
}

void Trail::Reset() {
  m_story.clear();
  m_scene = 0;
  m_play = 0;
  m_cast.clear();
  m_tracked = 0;
  m_restrictions.clear();
  m_prefabs.clear();
  m_gates = -1;
  m_lastPhase = 0;
}

std::string Trail::Now() const {
  std::string ours = "no story state";
  if (const auto it = m_story.find(proto::kNoPlayer); it != m_story.end())
    ours = std::format(
        "tier {}, fast travel {}, flags {}", it->second.tier,
        it->second.flags & proto::kStoryNoFastTravel ? "locked" : "open",
        StoryFlags(static_cast<std::uint8_t>(it->second.flags & ~proto::kStoryNoFastTravel)));
  return std::format("district {}; scene {}{}; tracked {}; last phase {}; restrictions {}; "
                     "prefabs {}; {}",
                     m_district.empty() ? "unknown" : m_district,
                     m_scene ? Hex(m_scene) : std::string("none"),
                     m_scene ? std::format(" (play {}, {} actors)", m_play, m_cast.size()) : "",
                     m_tracked ? Hex(m_tracked) : std::string("none"),
                     m_lastPhase ? Hex(m_lastPhase) : std::string("none"), m_restrictions.size(),
                     m_prefabs.size(), ours);
}

// ---- Tally ----

void Tally::Add(Moment moment) {
  const auto i = static_cast<std::size_t>(moment.kind);
  if (i < kTriggers)
    ++m_counts[i];
  // Worst first; an equal one later stays behind the earlier.
  const auto at = std::ranges::find_if(
      m_worst, [&](const Moment &kept) { return kept.severity < moment.severity; });
  if (at == m_worst.end() && m_worst.size() >= kKeep)
    return;
  m_worst.insert(at, std::move(moment));
  if (m_worst.size() > kKeep)
    m_worst.pop_back();
}

std::vector<Moment> Tally::Worst(std::size_t n) const {
  return {m_worst.begin(),
          m_worst.begin() + static_cast<std::ptrdiff_t>(std::min(n, m_worst.size()))};
}

std::string Tally::Summary(std::string_view title, const Trail &trail, std::uint32_t captures,
                           const std::function<std::string(std::uint64_t)> &clock) const {
  std::string out = std::format("== {} ==\n", title);
  std::string counts;
  std::uint32_t total = 0;
  for (std::size_t i = 0; i < kTriggers; ++i)
    if (m_counts[i]) {
      counts += std::format("{}{} {}", counts.empty() ? "" : ", ", kNames[i], m_counts[i]);
      total += m_counts[i];
    }
  out += std::format("triggers: {} ({}); captures written: {}\n", total,
                     counts.empty() ? "none" : counts, captures);
  out += "worst moments:\n";
  if (m_worst.empty())
    out += "  none\n";
  for (std::size_t i = 0; i < m_worst.size(); ++i) {
    const auto &moment = m_worst[i];
    out += std::format("  {}. {} {} ({:.0f}): {}{}\n", i + 1, clock(moment.us), NameOf(moment.kind),
                       moment.severity, moment.detail.substr(0, 200),
                       moment.capture.empty() ? "" : " [" + moment.capture + "]");
  }
  out += std::format("districts: {}\n",
                     Listed(trail.Districts(), 32, [](const std::string &name) { return name; }));
  out += std::format("journal entries activated: {}\n",
                     Listed(trail.Quests(), 32, [](std::uint32_t hash) { return Hex(hash); }));
  out += std::format("scenes: {}\n", Listed(trail.Scenes(), 32, Hex));
  out += std::format("quest phases first run: {}\n", trail.Phases());
  return out;
}

std::string Field(std::string_view text) {
  std::string out(text);
  for (auto &c : out)
    if (c == '\t' || c == '\n' || c == '\r')
      c = ' ';
  return out;
}

const char *JournalStateName(std::uint8_t state) {
  constexpr const char *kStates[] = {"undefined", "inactive", "active", "succeeded", "failed"};
  return state < std::size(kStates) ? kStates[state] : "?";
}
} // namespace tandem::observer
