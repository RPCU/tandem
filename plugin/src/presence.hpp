#pragma once

// Presence between the threads (docs/architecture/presence.md): our V's
// health and menu state on their way out, the others' on their way in, the
// cues and NPC hit reactions of the moment, the status effects that show on
// each V (StatusVisuals), and for each remote V the
// observer that hears it from what its game already sends
// (tandem/presence.hpp) and the flinch laid over its pose. Each piece says
// which threads touch it.

#include "tandem/consistency.hpp"
#include "tandem/presence.hpp"
#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace tandem::plugin {
class PresenceState {
public:
  static PresenceState &Get();

  // ---- game thread -> net thread ----
  // Our V's presence; true when it is worth sending now (presence::Gate).
  bool SubmitLocal(std::uint8_t health, std::uint8_t flags, std::uint64_t nowUs);
  std::optional<proto::Presence> TakeLocal();
  void SubmitCue(const proto::Cue &cue);
  std::vector<proto::Cue> TakeLocalCues();
  // A hit our V took, for the others' puppets of it.
  void SubmitHitTaken(const proto::HitTaken &hit);
  std::vector<proto::HitTaken> TakeLocalHitsTaken();
  // The status effects that show on our V (any order); true when the set
  // changed (it goes out then).
  bool SubmitStatusVisuals(std::vector<std::uint64_t> records);
  std::optional<std::vector<std::uint64_t>> TakeStatusVisuals();

  // ---- net thread -> game thread ----
  void PushRemote(const proto::Presence &presence);
  std::optional<proto::Presence> Remote(proto::PlayerId player);
  void PushCue(const proto::Cue &cue, std::uint64_t nowUs);
  // Cues half a second old (a loading screen, a stall) are not drawn.
  std::vector<proto::Cue> TakeCues(std::uint64_t nowUs);
  // The hits other Vs took; as cues, one half a second old is not shown.
  void PushHitTaken(const proto::HitTaken &hit, std::uint64_t nowUs);
  std::vector<proto::HitTaken> TakeHitsTaken(std::uint64_t nowUs);
  void PushReaction(const proto::NpcReaction &reaction);
  std::vector<proto::NpcReaction> TakeReactions();
  // The voice lines of NPCs we show (NpcVoice); one kNpcVoiceMaxAgeUs old
  // is not said (consistency::DropStale).
  void PushVoice(const proto::NpcVoice &voice, std::uint64_t nowUs);
  std::vector<proto::NpcVoice> TakeVoices(std::uint64_t nowUs);

  // ---- any thread (the voice wrap may run on a job thread) -> game thread ----
  // A voice line the game started on one of our entities (NpcVoices.reds):
  // its entity, context and delay (a trigger: its base name, variation and
  // their number, kVoiceTrigger); the tick sends those of our shared NPCs.
  struct LocalVoice {
    std::uint64_t entity{};
    std::uint64_t context{};
    std::uint16_t delayMs{};
    std::uint8_t flags{};
    std::uint16_t variation{};
    std::uint16_t variations{};
    std::uint8_t voContext{};
  };
  void QueueLocalVoice(const LocalVoice &voice, std::uint64_t nowUs);
  std::vector<LocalVoice> TakeLocalVoices(std::uint64_t nowUs);
  void PushStatusVisuals(const proto::StatusVisuals &visuals);
  // Another player's set as last heard (empty: none, or never heard), and a
  // count that moves on every change, so scripts look only then.
  std::vector<std::uint64_t> StatusVisualsOf(proto::PlayerId player);
  std::uint32_t StatusChanges();

  // ---- game thread only ----
  presence::Observer &ObserverOf(proto::PlayerId player) { return m_observers[player]; }

  // ---- game thread sets, animation job threads read ----
  void Flinch(proto::PlayerId player, float strength, const proto::Vec3 &direction,
              const proto::Vec3 &forward, std::uint64_t nowUs);
  presence::Lean LeanOf(proto::PlayerId player, std::uint64_t nowUs);

  // A player left; the session ended.
  void Forget(proto::PlayerId player);
  void Reset();

private:
  std::mutex m_lock;
  presence::Gate m_gate;                                                // game thread
  std::optional<proto::Presence> m_local;                               // m_lock
  std::vector<proto::Cue> m_localCues;                                  // m_lock
  std::map<proto::PlayerId, proto::Presence> m_remote;                  // m_lock
  std::vector<std::pair<proto::Cue, std::uint64_t>> m_cues;             // m_lock
  std::vector<proto::HitTaken> m_localHitsTaken;                        // m_lock
  std::vector<std::pair<proto::HitTaken, std::uint64_t>> m_hitsTaken;   // m_lock
  std::vector<proto::NpcReaction> m_reactions;                          // m_lock
  std::vector<consistency::Timed<proto::NpcVoice>> m_voices;            // m_lock
  std::vector<consistency::Timed<LocalVoice>> m_localVoices;            // m_lock
  std::vector<std::uint64_t> m_statusSent;                              // game thread
  std::optional<std::vector<std::uint64_t>> m_statusLocal;              // m_lock
  std::map<proto::PlayerId, std::vector<std::uint64_t>> m_statusRemote; // m_lock
  std::uint32_t m_statusChanges{};                                      // m_lock
  std::map<proto::PlayerId, presence::Observer> m_observers;            // game thread
  std::mutex m_flinchLock;
  std::map<proto::PlayerId, presence::Flinch> m_flinches; // m_flinchLock
};
} // namespace tandem::plugin
