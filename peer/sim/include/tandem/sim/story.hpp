#pragma once

// The host's story, scripted (docs/architecture/sim.md): a quest played from a
// .story file, the format of tandem-peer --story
// (tools/scenarios/story-host.story) with a few more verbs, by a director
// that waits for the guests where a real quest would: a call rings until
// someone answers, a hub stays open until someone suggests.
//
// One step per line, `#` comments:
//   SECONDS VERB ARGS...    seconds on the story's own clock
// Verbs (hashes are journal path hashes, names CNames or TweakDB records):
//   snapshot HASH:STATE ...      the journal as a joiner gets it
//   journal HASH STATE [notify]  one change (notify: the toast and sound)
//   tracked HASH                 the tracked entry
//   call CONTACT PHASE           the host's call: 1 ringing, 2 started, 3 ended
//   line TYPE SPEAKER TEXT...    a dialogue line (type 1 regular, 2 holocall;
//                                SPEAKER: host for our V, or a name)
//   hide                         the last line goes
//   hub ID TITLE|OPTION|...      a choice hub (no options: closed)
//   pick ID INDEX|auto           the host picks (auto: what a guest
//                                suggested, else the first)
//   tier N                       the host's scene tier (1..5)
//   reward RECORD [QUESTHASH]    a quest reward for the guests taking part
//   effect RECORD on|off         a story status effect on the host's V
//   replacer on|off              the host plays as Johnny
//   private on|off               the host is in a private scene
//   world PLACE [moved] [settled] the host's V in another world (cyberspace,
//                                braindance, johnny, other; city: back)
// The director's own (the mock player's --story logs them as not understood):
//   await answer|suggest SECS    the clock stops until a guest answers the
//                                call (suggests an option), SECS at most
//   cinematic SECS               a cinematic: tier 5 for SECS, then tier 1
//   loop SECS                    start again SECS later (rewards are paid once)

#include "tandem/sim/coverage.hpp"
#include "tandem/sim/scene.hpp"
#include "tandem/story.hpp"

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace tandem::sim {
struct StoryStep {
  double at{};
  std::string verb;
  std::vector<std::string> args;
  std::string rest; // everything after the verb
};

std::vector<StoryStep> ParseStory(std::istream &in, const std::string &name, std::string &error);
std::vector<StoryStep> LoadStory(const std::string &path, std::string &error);
// The quest the sim plays when it is given none: every story message once.
std::vector<StoryStep> DefaultStory();

proto::Place PlaceNamed(const std::string &name);
// One step as the host's game would send it (nullopt: not a message, or not
// understood). `lastLine`: the last dialogue line's id, counted here.
std::optional<proto::Message> StoryMessage(const StoryStep &step, std::uint64_t &lastLine,
                                           proto::PlayerId host);

class StoryDirector {
public:
  StoryDirector(std::vector<StoryStep> steps, Coverage *coverage);

  // What guests do about the story (events::Story on the host).
  void On(const SessionEvent &event);
  // Plays the steps that are due, while a guest is in the session.
  void Tick(std::uint64_t nowUs, const Scene &scene, const SeenPlayer &self, Outbox &out);

  // What the story does to our own V now.
  std::uint8_t Tier() const { return m_tier; }
  bool Private() const { return m_private; }
  bool Away() const { return m_away; }
  std::uint32_t Laps() const { return m_laps; }
  std::size_t Played() const { return m_played; }
  std::size_t NotUnderstood() const { return m_notUnderstood; }

private:
  void Play(const StoryStep &step, std::uint64_t nowUs, const Scene &scene, const SeenPlayer &self,
            Outbox &out);
  void Hit(std::string_view feature) {
    if (m_coverage)
      m_coverage->Hit(feature);
  }

  std::vector<StoryStep> m_steps;
  Coverage *m_coverage{};
  std::size_t m_next{};
  double m_clock{}; // seconds on the story's clock
  std::uint64_t m_lastUs{};
  std::uint64_t m_lastLine{};
  // await: what for, and when the wait gives up.
  enum class Wait { None, Answer, Suggest } m_wait{};
  std::uint64_t m_waitUntilUs{};
  bool m_answered{};
  std::optional<std::int8_t> m_suggested;
  std::optional<std::uint64_t> m_cinemaUntilUs, m_restartUs;
  std::uint8_t m_tier{1};
  bool m_private{}, m_away{};
  std::uint32_t m_laps{};
  std::size_t m_played{}, m_notUnderstood{};
  story::Participation m_participation;
};
} // namespace tandem::sim
