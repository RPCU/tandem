#pragma once

// Gameplay recordings: a real V's play, as its game sent it, replayed by
// tandem-peer as its own player (docs/architecture/recordings.md, "Gameplay
// recordings").
//
// A recording (.tplay) is a stream of real protocol messages with their
// time: everything the game asked its Endpoint to send about its own V
// (Endpoint::SetTap): states, the pose stream, look, held items, shots,
// seats and cars, and its actions (hits it dealt, quickhacks, takedowns,
// Sandevistan...). Replaying hands the same messages to another Endpoint's
// same calls, so the replayed player goes through the whole real path.
//
// A session recording (.tsession, Kind::Session) is the other side: every
// message a real host sent to one guest (tandem-peer join --record-session),
// replayed by a mock host to a real guest (host --play-session): the host's
// V, its NPCs, its story, its world, its hits on us.
//
// File: header {u32 "TPLY", u16 version, u16 protocol version, u64 the host
// clock when recording began, u32 the recorder's player id, u8 its body;
// version 2: u8 kind, u32 the host's player id}, then per message {u64
// microseconds since the start, u32 length, the encoded message}
// (proto::Encode). Version 1 files are plays.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tandem {
class Endpoint;
}

namespace tandem::play {
inline constexpr std::uint32_t kMagic = 0x594C5054; // "TPLY"
inline constexpr std::uint16_t kVersion = 2;

enum class Kind : std::uint8_t {
  Play = 0,    // a V's own play, as its game sent it (.tplay)
  Session = 1, // what a host sent to a guest (.tsession)
};

struct Header {
  std::uint16_t version{kVersion};
  std::uint16_t protocol{proto::kProtocolVersion};
  std::uint64_t startHostUs{}; // the recorder's host clock at the start
  proto::PlayerId player{};    // the recorder's player id in its session
  proto::Body body{};
  Kind kind{Kind::Play};
  proto::PlayerId host{}; // Session: the host's player id
};

struct Entry {
  std::uint64_t atUs{}; // since the start
  proto::Message message;
};

struct Recording {
  Header header;
  std::vector<Entry> entries;
  std::size_t skipped{}; // messages that did not decode (another protocol version)
  std::uint64_t DurationUs() const { return entries.empty() ? 0 : entries.back().atUs; }
  // Where the V was first, and the time it was there.
  std::optional<proto::PlayerState> FirstState() const;
  std::size_t Count(std::size_t messageIndex) const; // entries of one Message alternative
};

// What a recording keeps: the messages about its own V a game sends.
bool Keeps(const proto::Message &message);

// Writes a recording (the plugin's net thread; tandem-peer --save-play).
class Writer {
public:
  bool Open(const std::filesystem::path &path, const Header &header, std::string &error);
  // `hostUs`: when it was sent, on the host clock (earlier than the start: at 0).
  void Add(std::uint64_t hostUs, const proto::Message &message);
  void Close();
  bool IsOpen() const { return m_out.is_open(); }
  std::size_t Count() const { return m_count; }
  std::uint64_t LastUs() const { return m_lastUs; }
  const std::filesystem::path &Path() const { return m_path; }

private:
  std::ofstream m_out;
  std::filesystem::path m_path;
  Header m_header;
  std::size_t m_count{};
  std::uint64_t m_lastUs{};
};

std::optional<Recording> Load(const std::filesystem::path &path, std::string &error);

// How a replay stands for the recorded player.
struct Mapping {
  proto::PlayerId self{};  // the recorder's id (Header::player)
  proto::PlayerId other{}; // who a recorded other player becomes (kNoPlayer: drop those)
  proto::Vec3 offset;      // added to every position (default: where it was recorded)
  // An NPC id of the recording for the replay's session (nullopt: drop the
  // message). `near`: where the hit landed, when the message says.
  std::function<std::optional<proto::NpcId>(proto::NpcId recorded, const proto::Vec3 *near)> npc;
};

// The message as the replay sends it, `shiftUs` after it was recorded (the
// absolute host times it carries move by as much); nullopt: nothing to send.
std::optional<proto::Message> Adapt(proto::Message message, const Mapping &mapping,
                                    std::int64_t shiftUs);

// Hands a kept message to the Endpoint call its game made.
void Apply(Endpoint &endpoint, const proto::Message &message);

// ---- sessions ----

// What a session recording keeps of what a guest hears: everything the host
// says about its V and its world, not the transport's own (welcome, roster,
// pings) nor the consistency checks (the replaying host makes its own).
bool KeepsSession(const proto::Message &message);

// How a replaying host stands for the recorded one.
struct SessionMapping {
  proto::PlayerId host{};  // the recorded host (Header::host)
  proto::PlayerId you{};   // the recording guest (Header::player)
  proto::PlayerId self{};  // the replaying host
  proto::PlayerId guest{}; // the real guest the recording's "you" becomes (kNoPlayer: none yet)
};

// The message as the replaying host says it, `shiftUs` after it was
// recorded; nullopt: nothing to say (another player's, or the transport's).
std::optional<proto::Message> AdaptSession(proto::Message message, const SessionMapping &mapping,
                                           std::int64_t shiftUs);

// Hands it to the host Endpoint call that sends it (`guest`: a reward's).
void ApplyHost(Endpoint &endpoint, const proto::Message &message, proto::PlayerId guest);

// A recording's messages as their time comes, once each, lap after lap.
class Player {
public:
  explicit Player(Recording recording, bool loop = false, std::uint64_t lapGapUs = 1'000'000);
  // The entries due `elapsedUs` after the replay began.
  std::vector<const Entry *> Due(std::uint64_t elapsedUs);
  bool Done() const { return m_done; }
  std::uint32_t Lap() const { return m_lap; }
  // When the current lap began (since the replay began).
  std::uint64_t LapStartUs() const { return m_lapStartUs; }
  const Recording &Of() const { return m_recording; }

private:
  Recording m_recording;
  bool m_loop{};
  std::uint64_t m_lapGapUs{};
  std::size_t m_next{};
  std::uint64_t m_lapStartUs{};
  std::uint32_t m_lap{};
  bool m_done{};
};
} // namespace tandem::play
