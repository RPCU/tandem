#include "tandem/sim/story.hpp"

#include "tandem/sim/names.hpp"

#include <format>
#include <fstream>
#include <sstream>

namespace tandem::sim {
namespace {
namespace p = proto;

std::uint32_t Hash32(const std::string &text) {
  return static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
}

constexpr const char *kDefaultStory = R"(# The sim's own quest: every story message once.
0 snapshot 1000001:2 1000002:2 1000003:3
1 tracked 1000002
3 journal 1000004 2 notify
5 call judy 1
5 await answer 8
6 call judy 2
7 line 2 Judy Hey, V. Got a minute?
10 line 1 host Sure, what's up?
12 hub 501 Judy|Tell me more.|Not now.
12 await suggest 6
13 pick 501 auto
14 hub 501
15 hide
15 call judy 3
17 tier 4
17 effect BaseStatusEffect.JohnnySicknessLow on
22 effect BaseStatusEffect.JohnnySicknessLow off
22 tier 1
24 private on
29 private off
31 cinematic 5
37 reward QuestRewards.MinigameMoney 1000002
38 journal 1000002 3 notify
40 replacer on
44 replacer off
46 world braindance moved settled
54 world city
56 loop 30
)";
} // namespace

std::vector<StoryStep> ParseStory(std::istream &in, const std::string &name, std::string &error) {
  std::vector<StoryStep> steps;
  std::string line;
  for (int number = 1; std::getline(in, line); ++number) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream words(line);
    StoryStep step;
    if (!(words >> step.at >> step.verb)) {
      error = std::format("{}:{}: expected SECONDS VERB", name, number);
      return {};
    }
    std::getline(words, step.rest);
    if (const auto first = step.rest.find_first_not_of(' '); first != std::string::npos)
      step.rest = step.rest.substr(first);
    std::istringstream rest(step.rest);
    for (std::string word; rest >> word;)
      step.args.push_back(word);
    steps.push_back(std::move(step));
  }
  return steps;
}

std::vector<StoryStep> LoadStory(const std::string &path, std::string &error) {
  std::ifstream in(path);
  if (!in) {
    error = std::format("cannot read {}", path);
    return {};
  }
  return ParseStory(in, path, error);
}

std::vector<StoryStep> DefaultStory() {
  std::istringstream in(kDefaultStory);
  std::string error;
  return ParseStory(in, "the default story", error);
}

p::Place PlaceNamed(const std::string &name) {
  if (name == "cyberspace")
    return p::Place::Cyberspace;
  if (name == "braindance")
    return p::Place::Braindance;
  if (name == "johnny")
    return p::Place::Johnny;
  if (name == "other")
    return p::Place::Other;
  return p::Place::City;
}

std::optional<p::Message> StoryMessage(const StoryStep &step, std::uint64_t &lastLine,
                                       p::PlayerId host) {
  const auto &a = step.args;
  try {
    if (step.verb == "snapshot") {
      p::Journal journal{.snapshot = true};
      for (const auto &pair : a) {
        const auto colon = pair.find(':');
        journal.entries.push_back({Hash32(pair.substr(0, colon)),
                                   static_cast<std::uint8_t>(std::stoi(pair.substr(colon + 1))),
                                   0});
      }
      return journal;
    }
    if (step.verb == "journal" && a.size() >= 2)
      return p::Journal{.entries = {{Hash32(a[0]), static_cast<std::uint8_t>(std::stoi(a[1])),
                                     static_cast<std::uint8_t>(a.size() > 2)}}};
    if (step.verb == "tracked" && !a.empty())
      return p::Tracked{Hash32(a[0])};
    if (step.verb == "call" && a.size() >= 2)
      return p::PhoneCall{.contact = CName(a[0]),
                          .phase = static_cast<std::uint8_t>(std::stoi(a[1])),
                          .mode = 1,
                          .flags = p::kCallRejectable | p::kCallShowAvatar};
    if (step.verb == "line" && a.size() >= 3) {
      p::Subtitle line{
          .line = ++lastLine, .type = static_cast<std::uint8_t>(std::stoi(a[0])), .duration = 4.f};
      if (a[1] == "host")
        line.speakerPlayer = host;
      else
        line.speakerName = a[1];
      const auto text = step.rest.find(a[1]);
      line.text = step.rest.substr(text + a[1].size() + 1);
      return line;
    }
    if (step.verb == "hide")
      return p::Subtitle{.line = lastLine, .flags = p::kLineHide};
    if (step.verb == "hub" && !a.empty()) {
      p::ChoiceHub hub{.hub = std::stoi(a[0])};
      const auto rest = step.rest.substr(step.rest.find(a[0]) + a[0].size());
      if (const auto bar = rest.find('|'); bar != std::string::npos) {
        hub.title = rest.substr(rest.find_first_not_of(' '), bar - rest.find_first_not_of(' '));
        std::size_t start = bar + 1;
        for (;;) {
          const auto next = rest.find('|', start);
          hub.options.push_back({rest.substr(start, next - start), 0});
          if (next == std::string::npos)
            break;
          start = next + 1;
        }
      }
      return hub;
    }
    if (step.verb == "pick" && a.size() >= 2)
      return p::ChoiceCursor{.hub = std::stoi(a[0]),
                             .index = static_cast<std::int8_t>(std::stoi(a[1])),
                             .kind = p::kCursorPicked};
    if (step.verb == "tier" && !a.empty())
      return p::StoryState{.tier = static_cast<std::uint8_t>(std::stoi(a[0]))};
    if (step.verb == "private" && !a.empty())
      return p::StoryState{.tier = static_cast<std::uint8_t>(a[0] == "on" ? 3 : 1),
                           .flags = a[0] == "on" ? p::kStoryPrivate : std::uint8_t{0}};
    if (step.verb == "replacer" && !a.empty())
      return p::StoryState{.tier = 1, .flags = a[0] == "on" ? p::kStoryReplacer : std::uint8_t{0}};
    if (step.verb == "effect" && a.size() >= 2)
      return p::StoryEffect{.effect = TweakDBID(a[0]), .applied = a[1] == "on"};
    if (step.verb == "world" && !a.empty()) {
      p::Elsewhere away{.place = PlaceNamed(a[0])};
      for (std::size_t i = 1; i < a.size(); ++i) {
        if (a[i] == "moved")
          away.flags |= p::kElsewhereMoved;
        else if (a[i] == "settled")
          away.flags |= p::kElsewhereSettled;
      }
      return away;
    }
    if (step.verb == "reward" && !a.empty())
      return p::QuestReward{.reward = TweakDBID(a[0]), .quest = a.size() > 1 ? Hash32(a[1]) : 0};
  } catch (const std::exception &) {
  }
  return std::nullopt;
}

StoryDirector::StoryDirector(std::vector<StoryStep> steps, Coverage *coverage)
    : m_steps(std::move(steps)), m_coverage(coverage) {
  if (m_coverage)
    for (const char *feature :
         {"story.snapshot",   "story.journal",       "story.tracked",       "story.call",
          "story.line",       "story.hide",          "story.hub",           "story.pick",
          "story.tier",       "story.reward",        "story.effect",        "story.replacer",
          "story.private",    "story.world",         "story.await",         "story.cinematic",
          "story.loop",       "story.await-timeout", "story.call-answered", "story.hub-suggested",
          "story.text-reply", "story.dial",          "story.reward-nobody", "story.reward-once"})
      m_coverage->Declare(feature);
}

void StoryDirector::On(const SessionEvent &event) {
  const auto *story = std::get_if<events::Story>(&event);
  if (!story)
    return;
  if (const auto *action = std::get_if<p::PhoneAction>(&story->message)) {
    if (action->action == p::kPhoneDial) {
      Hit("story.dial");
    } else {
      m_answered = true;
      Hit("story.call-answered");
    }
  } else if (const auto *cursor = std::get_if<p::ChoiceCursor>(&story->message)) {
    if (cursor->kind == p::kCursorSuggest) {
      m_suggested = cursor->index;
      Hit("story.hub-suggested");
    }
  } else if (std::holds_alternative<p::PhoneReply>(story->message)) {
    Hit("story.text-reply");
  }
}

void StoryDirector::Tick(std::uint64_t nowUs, const Scene &scene, const SeenPlayer &self,
                         Outbox &out) {
  const double dt = m_lastUs == 0 ? 0. : static_cast<double>(nowUs - m_lastUs) / 1e6;
  m_lastUs = nowUs;
  // A story is told to someone: it waits while we are alone.
  if (scene.players.empty())
    return;
  if (m_cinemaUntilUs && nowUs >= *m_cinemaUntilUs) {
    m_cinemaUntilUs.reset();
    m_tier = 1;
    out.push_back({p::StoryState{.tier = 1}});
  }
  if (m_wait != Wait::None) {
    const bool came = m_wait == Wait::Answer ? m_answered : m_suggested.has_value();
    if (!came && nowUs < m_waitUntilUs)
      return; // the story's clock stands still
    if (!came)
      Hit("story.await-timeout");
    m_wait = Wait::None;
  }
  m_clock += dt;
  if (m_next == m_steps.size() && m_restartUs && nowUs >= *m_restartUs) {
    m_restartUs.reset();
    m_next = 0;
    m_clock = 0.;
    ++m_laps;
  }
  while (m_next < m_steps.size() && m_steps[m_next].at <= m_clock && m_wait == Wait::None)
    Play(m_steps[m_next++], nowUs, scene, self, out);
}

void StoryDirector::Play(const StoryStep &step, std::uint64_t nowUs, const Scene &scene,
                         const SeenPlayer &self, Outbox &out) {
  const auto &a = step.args;
  const auto seconds = [&](std::size_t index, double fallback) {
    try {
      return index < a.size() ? std::stod(a[index]) : fallback;
    } catch (const std::exception &) {
      return fallback;
    }
  };
  // The guests taking part: near our V now (story.hpp, "taking part").
  std::vector<p::PlayerId> near;
  for (const auto &[id, player] : scene.players)
    if (player.heard && Distance(player.position, self.position) <= story::kNearMetres)
      near.push_back(id);

  ++m_played;
  if (step.verb == "await" && !a.empty()) {
    m_wait = a[0] == "answer" ? Wait::Answer : Wait::Suggest;
    m_waitUntilUs = nowUs + static_cast<std::uint64_t>(seconds(1, 5.) * 1e6);
    Hit("story.await");
    return;
  }
  if (step.verb == "cinematic") {
    m_tier = 5;
    m_cinemaUntilUs = nowUs + static_cast<std::uint64_t>(seconds(0, 5.) * 1e6);
    out.push_back({p::StoryState{.tier = 5}});
    Hit("story.cinematic");
    return;
  }
  if (step.verb == "loop") {
    m_restartUs = nowUs + static_cast<std::uint64_t>(seconds(0, 30.) * 1e6);
    Hit("story.loop");
    return;
  }
  auto resolved = step;
  if (step.verb == "pick" && a.size() >= 2 && a[1] == "auto")
    resolved.args[1] = std::to_string(m_suggested.value_or(0));
  auto message = StoryMessage(resolved, m_lastLine, self.id);
  if (!message) {
    ++m_notUnderstood;
    return;
  }
  Hit("story." + step.verb);
  if (const auto *journal = std::get_if<p::Journal>(&*message)) {
    if (!journal->snapshot && !journal->entries.empty())
      m_participation.Note(journal->entries.front().hash, near, nowUs);
  } else if (const auto *call = std::get_if<p::PhoneCall>(&*message)) {
    if (call->phase == 1)
      m_answered = false;
  } else if (const auto *hub = std::get_if<p::ChoiceHub>(&*message)) {
    if (!hub->options.empty())
      m_suggested.reset();
  } else if (const auto *state = std::get_if<p::StoryState>(&*message)) {
    m_tier = state->tier;
    m_private = state->flags & p::kStoryPrivate;
  } else if (auto *away = std::get_if<p::Elsewhere>(&*message)) {
    // Another world is our V's own state: its body stays where it stood.
    m_away = away->place != p::Place::City;
    away->anchor = self.position;
    away->yaw = self.yaw;
  } else if (auto *reward = std::get_if<p::QuestReward>(&*message)) {
    // Paid once, to the guests who took part; a replayed quest pays nothing.
    if (m_laps > 0) {
      Hit("story.reward-once");
      return;
    }
    const auto award = m_participation.Attribute(nowUs, near);
    if (reward->quest == 0)
      reward->quest = award.quest;
    if (award.players.empty())
      Hit("story.reward-nobody");
    for (const auto player : award.players)
      out.push_back({*message, player});
    return;
  }
  out.push_back({std::move(*message)});
}
} // namespace tandem::sim
