#pragma once

// No silent fallback (docs/architecture/degradations.md): one registry of
// what a game runs without. Whatever takes a lesser path than the intended
// one says so here, by an id of the catalogue below:
//
// - a *state* holds for a while (an engine feature this game version turned
//   off, a path tandem.ini switched off): listed while it holds, logged as a
//   warning when it starts, shown to the player and told to the others
//   (proto::Degraded);
// - a *fallback* is taken for one body, once (a prompt drawn instead of the
//   game's own, a vehicle teleported instead of moved): counted, with its
//   last reason.
//
// Core entries are what co-op cannot be right without: a game with one
// hosts and joins nothing (Runtime::Blocked). The others are degraded: the
// session runs, and nobody in it is left unaware. An entry the player chose
// (tandem.ini) is no failure, and still listed.
//
// The plugin holds the one registry (plugin/src/degradations.hpp); scripts
// reach it through TandemNative.Degraded / DegradedOver / Fallback.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::degraded {

enum class Class : std::uint8_t {
  Core,     // co-op cannot be right without it: no session
  Degraded, // works, not as intended: visible to every player
};
enum class Kind : std::uint8_t {
  State,    // holds until it ends
  Fallback, // taken once, counted
};

// What may be registered: an id (1 to proto::kMaxDegradedId lower-case
// letters, digits, spaces, dashes; an engine feature's is its name in
// plugin/src/engine/access.hpp), its class, and what the lesser path is, in
// a few words for the log. Each id has its texts in Text.reds
// (`Degraded-<id with dashes>`) and its row in degradations.md
// (`task degraded:check`).
struct Info {
  std::string_view id;
  Class klass;
  Kind kind;
  std::string_view means;
};
std::span<const Info> Catalogue();
const Info *Find(std::string_view id);

struct Entry {
  std::string id;
  Class klass{Class::Degraded};
  Kind kind{Kind::State};
  bool chosen{}; // the player's own tandem.ini switch
  bool known{};  // an id of the catalogue
  std::string reason;
  std::uint32_t count{}; // a fallback's times; a state's starts
  friend bool operator==(const Entry &, const Entry &) = default;
};

// Any thread.
class Registry {
public:
  // A state starts (or goes on, with its first reason kept). True when it
  // was not holding: the caller logs it. An id the catalogue lacks is taken
  // as degraded (and listed as unknown, which fails every test). A state the
  // player chose never replaces one that was not chosen.
  bool Start(std::string_view id, std::string_view reason, bool chosen = false);
  // It holds no more. True when it was holding.
  bool End(std::string_view id);
  // A fallback was taken: its count so far.
  std::uint32_t Count(std::string_view id, std::string_view reason);
  // The fallbacks so far are forgotten (a test's start); states stay.
  void ResetFallbacks();

  // States that hold, then fallbacks taken, each in the catalogue's order
  // (unknown ids last).
  std::vector<Entry> List() const;
  // The core states that hold and nobody chose: no session with one.
  std::vector<Entry> CoreOff() const;
  // Bumped whenever List() would answer something else.
  std::uint64_t Changes() const;
  // The states that hold, as the session is told.
  proto::Degraded Wire() const;

  // One line: `degraded: none`, or `degraded: puppet movement (core),
  // vehicle move (chosen); fallbacks: drawn prompts x3`.
  std::string Summary() const;
  // Every entry on a line of its own, between `degraded: registry: …` and
  // `degraded: registry end` (tools/game.sh degraded reads them):
  //   degraded: state vehicle move [degraded, chosen]: tandem.ini kinematic_ride = off
  //   degraded: fallback drawn prompts [degraded] x3: no interaction layer
  std::vector<std::string> Report() const;

private:
  mutable std::mutex m_lock;
  std::vector<Entry> m_states;
  std::vector<Entry> m_fallbacks;
  std::uint64_t m_changes{};
  std::vector<Entry> ListLocked() const;
};

// The sentence of a core feature that is off, for the co-op menu's popup and
// the log (Runtime::Problems): it begins with kCoreProblem, by which the
// scripts know it from a startup problem and say it in the player's
// language instead (Coop.reds, Text.reds `Degraded-CoreOff`).
inline constexpr std::string_view kCoreProblem = "This game cannot run co-op: ";
std::string CoreProblem(const Entry &entry);
// The line of one entry (Report's).
std::string LineOf(const Entry &entry);
// What a player's game said, for a log: `puppet movement (core), looks`;
// "nothing" for an empty list.
std::string Describe(const proto::Degraded &said);
// An id as the wire takes it (proto::Fits): lower case, other characters as
// dashes, cut at proto::kMaxDegradedId.
std::string WireId(std::string_view id);
} // namespace tandem::degraded
