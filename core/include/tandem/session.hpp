#pragma once

// One end of a connection over ENet: a host listening for guests, or a guest
// connected to a host. Single-threaded: call Poll() regularly from one thread
// (the plugin runs it on its network thread, the mock player in its main loop).

#include "tandem/protocol.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tandem::net {
// Microseconds on a steady clock: only differences and the synced host offset
// matter.
std::uint64_t NowUs();

// Stable while the peer is connected; 1-based so 0 means "none".
using PeerId = std::uint32_t;

// A connection that acknowledges nothing for this long is dead: its process
// crashed or was killed, or its network is gone. ENet pings an idle peer every
// tenth of it, so a live one is heard from well within it, and a hiccup of a
// second or two passes.
inline constexpr std::chrono::milliseconds kDefaultTimeout{5'000};

struct Event {
  enum class Kind {
    Connected,
    Disconnected,
    Received,
    // A packet that does not decode: another protocol version or garbage.
    Malformed,
  };

  Kind kind{};
  PeerId peer{};
  std::optional<proto::Message> message;
};

struct Stats {
  std::uint32_t rttMs{};
  std::uint32_t rttVarianceMs{};
  float packetLoss{}; // 0..1
  // Since the peer last acknowledged anything (ENet pings an idle peer).
  std::uint32_t quietMs{};
};

class Session {
public:
  // `maxConnections` counts connections, welcomed or not: room for a
  // returning player or one to tell the session is full is the caller's.
  // `timeout`: see kDefaultTimeout (tests shorten it).
  static std::unique_ptr<Session> Listen(std::uint16_t port, std::size_t maxConnections,
                                         std::string &error,
                                         std::chrono::milliseconds timeout = kDefaultTimeout);
  // A host that does not answer within `timeout` is reported Disconnected.
  static std::unique_ptr<Session> Connect(const std::string &host, std::uint16_t port,
                                          std::string &error,
                                          std::chrono::milliseconds timeout = kDefaultTimeout);

  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  // Services the connection, waiting up to `timeout` for the first event. A
  // peer silent for longer than the session's timeout is dropped and
  // reported Disconnected, like one ENet timed out.
  std::vector<Event> Poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

  bool Send(PeerId peer, const proto::Message &message);
  void Broadcast(const proto::Message &message, PeerId except = 0);
  // A goodbye, once what was sent to the peer has arrived; it is reported
  // Disconnected when done.
  void Disconnect(PeerId peer);
  // At once, with no Disconnected event: for a connection replaced by another.
  void Drop(PeerId peer);
  void Flush();

  std::optional<Stats> StatsOf(PeerId peer) const;
  std::vector<PeerId> Peers() const;
  bool IsHost() const { return m_isHost; }

private:
  struct Impl;
  Session(std::unique_ptr<Impl> impl, bool isHost);

  std::unique_ptr<Impl> m_impl;
  bool m_isHost{};
};
} // namespace tandem::net
