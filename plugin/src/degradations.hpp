#pragma once

// The game's one registry of what it runs without (tandem/degraded.hpp,
// docs/architecture/degradations.md), with its log lines, what the other
// players' games said, and what the player has not been told yet.
//
// Nothing here calls the engine, and every function takes one short lock of
// its own: any thread may register (an engine check, a hook on a job thread,
// the net thread).

#include "tandem/degraded.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::plugin {
class Degradations {
public:
  static Degradations &Get();

  // A state starts: logged as a warning (an error for a core one) with its
  // reason, once, and queued for the player. `chosen`: tandem.ini's switch.
  void Start(std::string_view id, std::string_view reason, bool chosen = false);
  // It holds no more.
  void End(std::string_view id);
  // A fallback was taken: counted; the first of an id is a warning and
  // queued for the player, the next ones debug lines.
  void Fallback(std::string_view id, std::string_view reason);
  // The registry itself (List, CoreOff, Wire, Summary, Report).
  const degraded::Registry &Local() const { return m_local; }
  // (tests) The fallbacks so far are forgotten.
  void ResetFallbacks();

  // What another player's game runs without, as the session said it (game
  // thread, as the scripts take the session's word); an empty list forgets
  // the player. Queued for the player when it changed.
  void NoteRemote(const proto::Degraded &said);
  std::vector<proto::Degraded> Remote() const;
  // A session starts: the states that hold are told to the player again;
  // nothing of the last session's players is kept.
  void BeginSession();
  void EndSession();

  // One thing to tell the player.
  struct News {
    proto::PlayerId player{}; // 0: this game
    degraded::Entry entry;
    bool over{}; // the state ended
  };
  // Game thread: what was queued since the last call.
  std::vector<News> TakeNews();

private:
  degraded::Registry m_local;
  mutable std::mutex m_lock;
  std::map<proto::PlayerId, proto::Degraded> m_remote;
  std::vector<News> m_news;
  void Queue(News news);
};
} // namespace tandem::plugin
