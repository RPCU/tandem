#include "relay_link.hpp"
#include "resolve.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <random>

namespace tandem::net {
namespace {
// Crockford's base32 (relay.codeAlphabet): no I, L, O or U.
constexpr std::string_view kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// relay/internal/relay/wire.go
constexpr std::uint8_t kMagic[4] = {'T', 'N', 'R', '1'};
constexpr std::size_t kHeader = 5;
constexpr std::size_t kDataHeader = kHeader + 2;
constexpr std::size_t kRegisterSize = 64;
constexpr std::size_t kMaxDatagram = 1500;
enum : std::uint8_t {
  kRegisterHost = 1,
  kRegisterGuest = 2,
  kRegistered = 3,
  kData = 4,
  kKeepalive = 5,
  kError = 6,
  kRequestCode = 7,
  kCode = 8,
  kResolveCode = 9,
  kSecret = 10,
  kRedirect = 11,
  kRejoinGuest = 12,
  kReclaimCode = 13,
  kAddresses = 14,
  kIntroduce = 15,
  kPunch = 16,
  kProbe = 17,
  kObserved = 18,
  kRegisterHostKeep = 19,
  kChallenge = 20,
  kRegisterHostOpen = 21,
  kHostId = 22,
  kHostBound = 23,
  kQueryHost = 24,
  kHostState = 25,
  kJoinHost = 26,
  kHostSession = 27,
};
// relay.FlagHostOpen: a guest may join by our host ID.
constexpr std::uint8_t kFlagHostOpen = 1;
// A host lookup of the Join list: one answer waited for this long, asked twice.
constexpr enet_uint32 kHostQueryMs = 700;
constexpr std::size_t kPunchSize = kHeader + 4 + 2 + 1;
constexpr std::size_t kObservedSize = kHeader + 4 + 1 + 4 + 2; // IPv4
// A kept code's seconds left: it lives while we are registered.
constexpr std::uint16_t kKeptLeft = 0xFFFF;
// One redirect within a relay network, one more to cross its regions.
constexpr int kMaxRedirects = 2;
// Host with several relays: each probed every 250 ms for a second at most
// (RelayTimes::chooseMs), the first to answer the soonest taken.
constexpr enet_uint32 kProbeEveryMs = 250;
// The connection check, once registered: two relay ports probed, 3 times
// 300 ms apart at most.
constexpr int kCheckTries = 3;
constexpr enet_uint32 kCheckEveryMs = 300;
// An open relay's proof of work: hashes tried per Pump (about a
// millisecond's), so the net thread's loop is never held up.
constexpr int kPowSlice = 2048;

// Direct paths: punches every 100 ms for 3 s (RelayTimes::punchMs); a path
// silent for 1.5 s (directSilentMs) while we send on it is dropped (the
// relay takes its traffic back), and a guest asks for another introduction
// a minute later.
constexpr enet_uint32 kPunchEveryMs = 100;
constexpr enet_uint32 kIntroduceAgainMs = 60'000;

constexpr enet_uint32 kResendMs = 500;
// Host: registration and code asked again this often, so a relay that
// restarted (and forgot us) has the session back within it.
constexpr enet_uint32 kRefreshMs = 20'000;
constexpr enet_uint32 kKeepaliveMs = 10'000; // the relay forgets a client after 2 min
constexpr enet_uint32 kCodeRetryMs = 1'000;
// Registered: a relay silent for RelayTimes::probeMs is asked again (our
// registration, answered at once); silent lostAfterProbeMs more, it is lost
// and the session holds its relayed players. A session's ENet pings each
// way every half second, so a live relay is not silent for a second. A
// host with no relayed guest hears only the answers to its probes: it asks
// every few seconds (idleProbeMs; its code comes back as soon after a
// restart).
// Host: a relayed guest's socket closes after this long without traffic.
constexpr enet_uint32 kChannelIdleMs = 60'000;
constexpr std::size_t kMaxChannels = 8;
constexpr std::size_t kMaxPending = 32;

enet_uint32 Since(enet_uint32 now, enet_uint32 then) { return now - then; }
bool Reached(enet_uint32 now, enet_uint32 when) {
  return static_cast<std::int32_t>(now - when) >= 0;
}

enet_uint32 Loopback() { return ENET_HOST_TO_NET_32(0x7F000001); }

bool Same(const ENetAddress &a, const ENetAddress &b) {
  return a.host == b.host && a.port == b.port;
}

std::vector<std::uint8_t> Header(std::uint8_t type) {
  return {kMagic[0], kMagic[1], kMagic[2], kMagic[3], type};
}

// Registration-type requests are padded: the relay's answer is never larger.
std::vector<std::uint8_t> Padded(std::vector<std::uint8_t> bytes) {
  bytes.resize(kRegisterSize);
  return bytes;
}

void AppendBe16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

std::string Text(const ENetAddress &address) {
  const auto ip = ENET_NET_TO_HOST_32(address.host);
  return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
         std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF) + ":" +
         std::to_string(address.port);
}

ENetSocket OpenSocket(const ENetAddress *bindTo) {
  ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (socket == ENET_SOCKET_NULL)
    return socket;
  if (enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1) < 0 ||
      (bindTo && enet_socket_bind(socket, bindTo) < 0)) {
    enet_socket_destroy(socket);
    return ENET_SOCKET_NULL;
  }
  return socket;
}

void Close(ENetSocket &socket) {
  if (socket != ENET_SOCKET_NULL)
    enet_socket_destroy(socket);
  socket = ENET_SOCKET_NULL;
}

void SendTo(ENetSocket socket, const ENetAddress &to, const std::uint8_t *data, std::size_t size) {
  ENetBuffer buffer;
  buffer.data = const_cast<std::uint8_t *>(data);
  buffer.dataLength = size;
  enet_socket_send(socket, &to, &buffer, 1);
}

// One datagram's size, 0 when none is waiting. Errors (a truncated
// datagram, a port-unreachable report) are skipped.
std::size_t ReceiveFrom(ENetSocket socket, ENetAddress &from, std::uint8_t *data,
                        std::size_t size) {
  for (int tries = 0; tries < 8; ++tries) {
    ENetBuffer buffer;
    buffer.data = data;
    buffer.dataLength = size;
    const int got = enet_socket_receive(socket, &from, &buffer, 1);
    if (got >= 0)
      return static_cast<std::size_t>(got);
  }
  return 0;
}

std::uint16_t Be16(const std::uint8_t *p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

std::uint32_t Be32(const std::uint8_t *p) {
  return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
         p[3];
}

// An IPv4 address and port as the wire has them (network order already).
ENetAddress AddressAt(const std::uint8_t *p) {
  ENetAddress address{};
  std::memcpy(&address.host, p, 4);
  address.port = Be16(p + 4);
  return address;
}

std::vector<std::uint8_t> PunchDatagram(std::uint32_t nonce, std::uint16_t channel, bool ack) {
  auto bytes = Header(kPunch);
  for (int shift = 24; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<std::uint8_t>(nonce >> shift));
  AppendBe16(bytes, channel);
  bytes.push_back(ack ? 1 : 0);
  return bytes;
}

void WrapData(std::uint8_t *datagram, std::uint16_t channel) {
  std::memcpy(datagram, kMagic, 4);
  datagram[4] = kData;
  datagram[5] = static_cast<std::uint8_t>(channel >> 8);
  datagram[6] = static_cast<std::uint8_t>(channel);
}
} // namespace

std::optional<std::string> NormalizeJoinCode(std::string_view text) {
  std::string code;
  for (char c : text) {
    if (c == '-' || c == ' ')
      continue;
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
    if (c == 'O')
      c = '0';
    else if (c == 'I' || c == 'L')
      c = '1';
    if (kAlphabet.find(c) == std::string_view::npos || code.size() == kJoinCodeSize)
      return std::nullopt;
    code += c;
  }
  if (code.size() != kJoinCodeSize)
    return std::nullopt;
  return code;
}

std::string DisplayJoinCode(std::string_view code) {
  if (code.size() != kJoinCodeSize)
    return std::string(code);
  return std::string(code.substr(0, 3)) + "-" + std::string(code.substr(3));
}

std::optional<RelayToken> ParseRelayToken(std::string_view hex) {
  RelayToken token{};
  if (hex.size() != token.size() * 2)
    return std::nullopt;
  for (std::size_t i = 0; i < token.size(); ++i) {
    const auto *first = hex.data() + 2 * i;
    auto [end, ec] = std::from_chars(first, first + 2, token[i], 16);
    if (ec != std::errc{} || end != first + 2)
      return std::nullopt;
  }
  return token;
}

HostId HostIdOf(const HostKey &key) {
  // relay.HostIDOf: SHA-256 of a label and the key, its first 16 bytes.
  constexpr std::string_view kLabel{"tandem-host-id\0", 15};
  std::vector<std::uint8_t> message(kLabel.begin(), kLabel.end());
  message.insert(message.end(), key.begin(), key.end());
  const auto digest = Sha256(message);
  HostId id{};
  std::copy_n(digest.begin(), id.size(), id.begin());
  return id;
}

std::string HostIdText(const HostId &id) {
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string text;
  for (const auto byte : id) {
    text += kHex[byte >> 4];
    text += kHex[byte & 15];
  }
  return text;
}

std::optional<HostId> ParseHostId(std::string_view hex) {
  HostId id{};
  if (hex.size() != id.size() * 2)
    return std::nullopt;
  for (std::size_t i = 0; i < id.size(); ++i) {
    const auto *first = hex.data() + 2 * i;
    auto [end, ec] = std::from_chars(first, first + 2, id[i], 16);
    if (ec != std::errc{} || end != first + 2)
      return std::nullopt;
  }
  return id;
}

std::vector<HostPresence> QueryHosts(const std::string &address, std::uint16_t port,
                                     const std::vector<HostId> &ids, std::uint32_t timeoutMs) {
  std::vector<HostPresence> out(ids.size(), HostPresence::Unknown);
  ENetAddress relay{};
  if (ids.empty() || !ResolveIpv4(address, relay))
    return out;
  relay.port = port;
  ENetSocket socket = OpenSocket(nullptr);
  if (socket == ENET_SOCKET_NULL)
    return out;
  const auto start = enet_time_get();
  // One at a time: an answer does not name the ID it is about.
  for (std::size_t i = 0; i < ids.size() && Since(enet_time_get(), start) < timeoutMs; ++i) {
    auto bytes = Header(kQueryHost);
    bytes.insert(bytes.end(), ids[i].begin(), ids[i].end());
    const auto request = Padded(std::move(bytes));
    ENetAddress to = relay;
    int redirects = 0, tries = 0;
    bool done = false;
    while (!done && tries < 2 && Since(enet_time_get(), start) < timeoutMs) {
      ++tries;
      SendTo(socket, to, request.data(), request.size());
      const auto sent = enet_time_get();
      while (!done && Since(enet_time_get(), sent) < kHostQueryMs) {
        enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
        if (enet_socket_wait(socket, &condition, 50) < 0 || !(condition & ENET_SOCKET_WAIT_RECEIVE))
          continue;
        std::uint8_t buffer[64];
        ENetAddress from{};
        const auto size = ReceiveFrom(socket, from, buffer, sizeof buffer);
        if (size < kHeader + 1 || !Same(from, to) || std::memcmp(buffer, kMagic, 4) != 0)
          continue;
        if (buffer[4] == kHostState && size == kHeader + 1 && buffer[kHeader] <= 2) {
          out[i] = static_cast<HostPresence>(buffer[kHeader]);
          done = true;
        } else if (buffer[4] == kRedirect && size == kHeader + 1 + 4 + 2 && buffer[kHeader] == 4 &&
                   redirects < kMaxRedirects) {
          std::memcpy(&to.host, buffer + kHeader + 1, 4); // network order already
          to.port = Be16(buffer + kHeader + 5);
          ++redirects;
          tries = 0;
          break; // asked again there
        } else if (buffer[4] == kError) {
          // Over the lookups' limit (or refused): the rest stay unknown.
          enet_socket_destroy(socket);
          return out;
        }
      }
    }
  }
  enet_socket_destroy(socket);
  return out;
}

std::string_view DirectOutlook(const RelayStatus &status) {
  switch (status.nat) {
  case NatMapping::Stable:
    return "direct";
  case NatMapping::Varies:
    return "relay";
  default:
    return "";
  }
}

bool ParseRelayAddress(std::string_view text, std::string &name, std::uint16_t &port) {
  port = kDefaultRelayPort;
  if (const auto colon = text.rfind(':'); colon != std::string_view::npos) {
    const auto digits = text.substr(colon + 1);
    auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), port);
    if (ec != std::errc{} || end != digits.data() + digits.size() || port == 0)
      return false;
    text = text.substr(0, colon);
  }
  name = std::string(text);
  return !name.empty();
}

RelayLink::RelayLink(Role role, RelayOptions options, std::uint16_t enetPort,
                     ENetAddress enetSource, std::uint32_t timeoutMs)
    : m_role(role), m_options(std::move(options)), m_timeoutMs(timeoutMs), m_enetSource(enetSource),
      m_directOn(m_options.direct) {
  m_startMs = enet_time_get();
  if (!ResolveIpv4(m_options.address, m_relay)) {
    Fail("unknown relay " + m_options.address);
    return;
  }
  m_relay.port = m_options.port;
  m_home = m_relay;
  m_homes.push_back(m_relay);
  for (const auto &more : m_options.more) {
    std::string name;
    std::uint16_t port{};
    ENetAddress address{};
    if (ParseRelayAddress(more, name, port) && ResolveIpv4(name, address)) {
      address.port = port;
      m_homes.push_back(address);
    }
  }
  std::random_device random;
  m_probeNonce = random() | 1;
  m_outer = OpenSocket(nullptr);
  if (m_outer == ENET_SOCKET_NULL) {
    Fail("could not open a socket to the relay");
    return;
  }

  if (m_role == Role::Guest) {
    ENetAddress local{};
    local.host = Loopback();
    m_inner = OpenSocket(&local);
    if (m_inner == ENET_SOCKET_NULL || enet_socket_get_address(m_inner, &m_innerAddress) < 0) {
      Fail("could not open the relay's local socket");
      return;
    }
    m_innerAddress.host = Loopback();
    if (m_options.secret) {
      m_secret = *m_options.secret;
      m_haveSecret = m_secretGiven = true;
      m_phase = Phase::Registering;
    } else if (m_options.hostId) {
      // A recent host, joined by its ID: no code (JoinHost).
      m_phase = Phase::Resolving;
    } else if (const auto code = NormalizeJoinCode(m_options.code)) {
      m_options.code = *code;
      m_phase = Phase::Resolving;
    } else {
      Fail(m_options.code.empty() ? "no join code" : "not a join code: " + m_options.code);
      return;
    }
  } else {
    m_enetListen.host = Loopback();
    m_enetListen.port = enetPort;
    // A new secret per session: the relay knows the session by it, and
    // only its host, from its own address, may ask for its code.
    for (std::size_t i = 0; i < m_secret.size(); i += 4) {
      const auto word = random();
      for (std::size_t j = 0; j < 4; ++j)
        m_secret[i + j] = static_cast<std::uint8_t>(word >> (8 * j));
    }
    m_haveSecret = true;
    m_phase = Phase::Registering;
    if (m_homes.size() > 1) {
      StartOver(m_startMs); // the nearest first
      return;
    }
  }
  SendRequest(m_startMs);
}

void RelayLink::Probe(const ENetAddress &to) {
  auto bytes = Header(kProbe);
  for (int shift = 24; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<std::uint8_t>(m_probeNonce >> shift));
  const auto padded = Padded(std::move(bytes));
  SendTo(m_outer, to, padded.data(), padded.size());
}

// A relay's answer to a probe (from any relay we asked): a round trip while
// choosing, and what it saw of us for the connection check.
bool RelayLink::OnObserved(const ENetAddress &from, const std::uint8_t *data, std::size_t size,
                           enet_uint32 now) {
  if (size < kHeader || std::memcmp(data, kMagic, 4) != 0 || data[4] != kObserved)
    return false;
  if (size != kObservedSize || Be32(data + kHeader) != m_probeNonce || data[kHeader + 4] != 4)
    return true; // IPv6, or not ours
  const auto us = AddressAt(data + kHeader + 5);
  const auto before = Mapping();
  const auto seen =
      std::ranges::find_if(m_seen, [&](const Seen &s) { return Same(s.relay, from); });
  if (seen != m_seen.end())
    seen->us = us;
  else if (m_seen.size() < 8)
    m_seen.push_back({from, us});
  if (m_phase == Phase::Choosing)
    for (std::size_t i = 0; i < m_homes.size(); ++i)
      if (Same(m_homes[i], from) && m_rttMs[i] == 0)
        m_rttMs[i] = std::max<enet_uint32>(Since(now, m_chooseMs), 1);
  // Known now: a guest asks for its introduction with it, a host tells it.
  if (before == NatMapping::Unknown && Mapping() != NatMapping::Unknown) {
    if (m_role == Role::Guest)
      m_introduceAgainMs = Reached(now, m_introduceAgainMs) ? m_introduceAgainMs : now;
    else
      m_addressesSent = false;
  }
  return true;
}

void RelayLink::Choose(enet_uint32 now) {
  std::size_t best = 0;
  for (std::size_t i = 0; i < m_homes.size(); ++i)
    if (m_rttMs[i] != 0 && (m_rttMs[best] == 0 || m_rttMs[i] < m_rttMs[best]))
      best = i;
  m_homeIndex = best;
  m_home = m_relay = m_homes[best];
  m_phase = Phase::Registering;
  m_startMs = now;
  SendRequest(now);
}

NatMapping RelayLink::Mapping() const {
  if (m_seen.size() < 2)
    return NatMapping::Unknown;
  const bool same =
      std::ranges::all_of(m_seen, [&](const Seen &s) { return Same(s.us, m_seen[0].us); });
  return same ? NatMapping::Stable : NatMapping::Varies;
}

bool RelayLink::NextHome(enet_uint32 now) {
  if (m_role != Role::Guest || m_homeIndex + 1 >= m_homes.size())
    return false;
  m_home = m_relay = m_homes[++m_homeIndex];
  m_redirects = 0;
  m_phase = Phase::Resolving;
  SendRequest(now);
  return true;
}

void RelayLink::NewCode() {
  if (m_role != Role::Host || m_phase != Phase::Ready)
    return;
  const auto now = enet_time_get();
  if (m_options.keepCode && !m_keepFailed) {
    // The next salt: registered again with it, the relay gives its code.
    ++m_options.codeSalt;
    SendRequest(now);
    m_codeRequestMs = now - kRefreshMs + 300;
    return;
  }
  auto bytes = Header(kRequestCode);
  bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
  bytes.push_back(1); // renew
  ToRelay(Padded(std::move(bytes)), now);
  m_codeRequestMs = now;
}

RelayLink::~RelayLink() {
  Close(m_outer);
  Close(m_inner);
  for (auto &[id, channel] : m_channels)
    Close(channel.socket);
}

void RelayLink::Fail(std::string error) {
  m_phase = Phase::Failed;
  m_lost = false;
  m_status.error = std::move(error);
  // A kept code's replica may be the trouble: the next try asks for any code.
  if (m_role == Role::Host && m_options.keepCode)
    m_keepFailed = true;
}

void RelayLink::ToRelay(const std::vector<std::uint8_t> &bytes, enet_uint32 now) {
  SendTo(m_outer, m_relay, bytes.data(), bytes.size());
  m_sentMs = now;
}

void RelayLink::SendRequest(enet_uint32 now) {
  std::vector<std::uint8_t> bytes;
  if (m_phase == Phase::Resolving && m_options.hostId) {
    bytes = Header(kJoinHost);
    bytes.insert(bytes.end(), m_options.hostId->begin(), m_options.hostId->end());
  } else if (m_phase == Phase::Resolving) {
    bytes = Header(kResolveCode);
    bytes.insert(bytes.end(), m_options.code.begin(), m_options.code.end());
  } else if (m_role == Role::Host && m_solved) {
    // An open relay's challenge, solved: the session opens with no token.
    bytes = Header(kRegisterHostOpen);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
    bytes.insert(bytes.end(), std::begin(m_challenge.stamp), std::end(m_challenge.stamp));
    bytes.push_back(m_challenge.bits);
    bytes.insert(bytes.end(), std::begin(m_challenge.mac), std::end(m_challenge.mac));
    for (int shift = 56; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<std::uint8_t>(m_powNonce >> shift));
  } else if (m_role == Role::Host && m_solving) {
    return; // the solver sends it once it holds
  } else if (m_role == Role::Host) {
    const bool keep = m_options.keepCode && !m_keepFailed;
    bytes = Header(keep ? kRegisterHostKeep : kRegisterHost);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
    bytes.insert(bytes.end(), m_options.token.begin(), m_options.token.end());
    if (keep)
      for (int shift = 24; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>(m_options.codeSalt >> shift));
  } else if (m_channel != 0) {
    // Back on a relay that may have restarted: our old channel, so the
    // host's proxy (and its ENet) knows us as before.
    bytes = Header(kRejoinGuest);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
    AppendBe16(bytes, m_channel);
  } else {
    bytes = Header(kRegisterGuest);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
  }
  ToRelay(Padded(std::move(bytes)), now);
  m_requestMs = now;
}

// An open relay asks a host with no token for a proof of work: a nonce
// such that SHA-256(mac | secret | nonce) starts with `bits` zero bits
// (relay/internal/relay/open.go). A new challenge (ours was refused: stale,
// or used) starts over; a late copy of the one being solved is ignored.
void RelayLink::OnChallenge(const std::uint8_t *body, std::size_t length, enet_uint32 now) {
  if (m_role != Role::Host || length != 4 + 1 + 16 ||
      (m_phase != Phase::Registering && m_phase != Phase::Ready))
    return;
  if ((m_solving || m_solved) && std::memcmp(body + 5, m_challenge.mac, 16) == 0)
    return;
  std::memcpy(m_challenge.stamp, body, 4);
  m_challenge.bits = body[4];
  std::memcpy(m_challenge.mac, body + 5, 16);
  m_solving = true;
  m_solved = false;
  m_powNonce = 0;
  Solve(now);
}

// A slice of the search, about a millisecond: Pump runs it each time until
// it holds, then the registration goes out at once.
void RelayLink::Solve(enet_uint32 now) {
  if (!m_solving)
    return;
  std::uint8_t message[16 + 16 + 8];
  std::memcpy(message, m_challenge.mac, 16);
  std::copy(m_secret.begin(), m_secret.end(), message + 16);
  for (int i = 0; i < kPowSlice; ++i, ++m_powNonce) {
    for (int j = 0; j < 8; ++j)
      message[32 + j] = static_cast<std::uint8_t>(m_powNonce >> (56 - 8 * j));
    if (LeadingZeroBits(Sha256(message)) >= m_challenge.bits) {
      m_solving = false;
      m_solved = true;
      SendRequest(now);
      return;
    }
  }
}

void RelayLink::StartOver(enet_uint32 now) {
  m_relay = m_home;
  m_redirects = 0;
  m_lost = false;
  m_startMs = now;
  m_status.code.clear(); // that replica's: another gives another
  m_codeKept = false;
  m_hostAt = {}; // our host ID bound again once registered
  m_hostBinding = false;
  m_solving = m_solved = false; // another relay, another challenge
  if (m_homes.size() > 1) {
    // Several relays: the one answering the soonest now.
    m_phase = Phase::Choosing;
    m_rttMs.assign(m_homes.size(), 0);
    m_chooseMs = m_probedMs = now;
    for (const auto &home : m_homes)
      Probe(home);
    return;
  }
  m_phase = Phase::Registering;
  SendRequest(now);
}

void RelayLink::RequestCode(enet_uint32 now) {
  // With a code still live, ask for that one: the relay keeps it, or gives
  // it back after it restarted (ReclaimCode), so the code players were
  // given goes on working.
  std::vector<std::uint8_t> bytes;
  if (!m_status.code.empty() && !Reached(now, m_codeExpiresMs)) {
    bytes = Header(kReclaimCode);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
    bytes.insert(bytes.end(), m_status.code.begin(), m_status.code.end());
    AppendBe16(bytes, static_cast<std::uint16_t>(
                          std::min<enet_uint32>((m_codeExpiresMs - now) / 1000, 0xFFFF)));
  } else {
    bytes = Header(kRequestCode);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
  }
  ToRelay(Padded(std::move(bytes)), now);
  m_codeRequestMs = now;
}

void RelayLink::Pump() {
  if (m_outer == ENET_SOCKET_NULL)
    return;
  const auto now = enet_time_get();
  // Room for the relay's header in front: ENet's datagrams are wrapped in place.
  std::uint8_t buffer[kDataHeader + kMaxDatagram];
  ENetAddress from{};

  for (int i = 0; i < 256; ++i) {
    const auto size = ReceiveFrom(m_outer, from, buffer, sizeof buffer);
    if (size == 0)
      break;
    if (OnObserved(from, buffer, size, now))
      continue; // a probe's answer, from any relay we asked
    if (m_role == Role::Host && m_hostBinding && Same(from, m_hostAt) && !Same(from, m_relay)) {
      OnHostReply(buffer, size, now); // our host ID's owner, another replica
      continue;
    }
    if (Same(from, m_relay) && m_phase != Phase::Choosing)
      OnRelay(buffer, size, now);
    else if (m_directOn)
      OnDirect(from, buffer, size, now); // the other side, on a direct path
  }

  if (m_role == Role::Guest && m_inner != ENET_SOCKET_NULL) {
    for (int i = 0; i < 256; ++i) {
      const auto size = ReceiveFrom(m_inner, from, buffer + kDataHeader, kMaxDatagram);
      if (size == 0)
        break;
      if (!Same(from, m_enetSource))
        continue; // only our own ENet speaks through the proxy
      WrapData(buffer, m_channel);
      if (m_direct.up || m_phase == Phase::Ready) {
        Forward(m_direct, buffer, kDataHeader + size, now);
      } else if (m_phase != Phase::Failed && m_pending.size() < kMaxPending) {
        m_pending.emplace_back(buffer, buffer + kDataHeader + size);
      }
    }
  }

  for (auto &[id, channel] : m_channels) {
    for (int i = 0; i < 256; ++i) {
      const auto size = ReceiveFrom(channel.socket, from, buffer + kDataHeader, kMaxDatagram);
      if (size == 0)
        break;
      if (!Same(from, m_enetListen) || (m_phase != Phase::Ready && !channel.direct.up))
        continue;
      WrapData(buffer, id);
      Forward(channel.direct, buffer, kDataHeader + size, now);
      channel.lastMs = now;
    }
  }

  Solve(now); // an open relay's proof of work, a slice at a time
  Timers(now);
}

void RelayLink::OnRelay(const std::uint8_t *data, std::size_t size, enet_uint32 now) {
  if (size < kHeader || std::memcmp(data, kMagic, 4) != 0)
    return;
  const auto quiet = Since(now, m_heardMs);
  m_heardMs = now;
  const auto *body = data + kHeader;
  const auto length = size - kHeader;
  switch (data[4]) {
  case kRegistered:
    if (length != 2)
      return;
    m_solving = m_solved = false; // in: a retry needs no proof
    if (m_phase == Phase::Ready) {
      // Our probe answered: the relay is there (again). It may have
      // restarted, our code with it: asked for at once.
      m_lost = false;
      if (m_role == Role::Guest) {
        m_channel = Be16(body);
      } else if (quiet >= m_options.times.probeMs) {
        m_codeRequestMs = now - kRefreshMs;
        m_addressesSent = false; // and what it knew of our addresses
      }
      return;
    }
    // A guest that gave up stays given up; a host takes a late answer.
    if (m_phase == Phase::Resolving || (m_role == Role::Guest && m_phase == Phase::Failed))
      return;
    m_phase = Phase::Ready;
    m_lost = false;
    if (m_role == Role::Guest)
      m_channel = Be16(body);
    m_status.error.clear();
    for (const auto &datagram : m_pending)
      SendTo(m_outer, m_relay, datagram.data(), datagram.size());
    m_pending.clear();
    m_sentMs = now;
    // The connection check: this relay port and a second one (the fleet's
    // entrance, or the next port: an entrance's by default).
    m_checkAlt = Same(m_home, m_relay)
                     ? ENetAddress{m_relay.host, static_cast<enet_uint16>(m_relay.port + 1)}
                     : m_home;
    m_checkTries = 0;
    m_checkMs = now - kCheckEveryMs;
    // The host tells the relay its addresses as it registers; a guest asks
    // for its introduction once the check is done (or gave up).
    if (m_directOn) {
      if (m_role == Role::Host)
        SendAddresses(now);
      else
        m_introduceAgainMs = now + kCheckTries * kCheckEveryMs;
    }
    break;
  case kIntroduce:
    if (m_phase == Phase::Ready && m_directOn)
      OnIntroduce(body, length, now);
    break;
  case kError:
    // Registered, a refusal answers a probe of a relay that restarted and
    // does not have our session yet (its host is not back): asked again
    // until the hold ends.
    if (length == 1 && m_phase != Phase::Ready)
      OnError(body[0]);
    break;
  case kRedirect:
    // Registered, a host is sent on only for its host ID (to its owner).
    if (m_role == Role::Host && m_phase == Phase::Ready && m_hostBinding)
      OnHostReply(data, size, now);
    else
      OnRedirect(body, length, now);
    break;
  case kHostBound:
    if (m_role == Role::Host)
      OnHostReply(data, size, now);
    break;
  case kHostState:
    // A guest joining by a host ID that may not: in words, no retry.
    if (m_role == Role::Guest && m_phase == Phase::Resolving && length == 1)
      Fail(body[0] == 1 ? "the host is not open to a join by its ID (invite only, or full)"
                        : "the host is not online");
    break;
  case kHostSession: {
    if (m_role != Role::Guest || m_phase != Phase::Resolving || length < m_secret.size() + 1)
      return;
    const auto *at = body + m_secret.size();
    const auto rest = length - m_secret.size();
    if (rest == 1 + 4 + 2 && at[0] == 4) {
      // The session is on another replica: registered there.
      std::memcpy(&m_relay.host, at + 1, 4); // network order already
      m_relay.port = Be16(at + 5);
    } else if (rest != 1 || at[0] != 0) {
      Fail("the relay sent us to an address we cannot reach (IPv6)");
      return;
    }
    std::copy_n(body, m_secret.size(), m_secret.begin());
    m_haveSecret = true;
    m_phase = Phase::Registering;
    SendRequest(now);
    break;
  }
  case kChallenge:
    OnChallenge(body, length, now);
    break;
  case kData: {
    if (length < 2)
      return;
    const auto *payload = body + 2;
    const auto payloadSize = length - 2;
    if (m_role == Role::Guest) {
      if (m_phase == Phase::Ready)
        SendTo(m_inner, m_enetSource, payload, payloadSize);
    } else if (auto *channel = ChannelFor(Be16(body), now)) {
      SendTo(channel->socket, m_enetListen, payload, payloadSize);
      channel->lastMs = now;
    }
    break;
  }
  case kCode: {
    if (m_role != Role::Host || length != kJoinCodeSize + 2)
      return;
    const std::string_view code(reinterpret_cast<const char *>(body), kJoinCodeSize);
    if (NormalizeJoinCode(code) != code)
      return;
    m_status.code = std::string(code);
    // A kept code lives while we are registered: no countdown.
    m_codeKept = Be16(body + kJoinCodeSize) == kKeptLeft;
    m_codeExpiresMs = now + (m_codeKept ? 86'400'000u : 1000u * Be16(body + kJoinCodeSize));
    break;
  }
  case kSecret:
    if (m_role != Role::Guest || m_phase != Phase::Resolving || length != m_secret.size())
      return;
    std::copy_n(body, m_secret.size(), m_secret.begin());
    m_haveSecret = true;
    m_phase = Phase::Registering;
    SendRequest(now);
    break;
  default:
    break;
  }
}

// Host: our host ID bound to our session, on its owner (the relay in use, or
// the replica it sends us to), with where the session is when that differs.
void RelayLink::BindHostId(enet_uint32 now) {
  if (!m_options.hostKey)
    return;
  if (m_hostAt.host == 0) {
    m_hostAt = m_relay;
    m_hostRedirects = 0;
  }
  auto bytes = Header(kHostId);
  bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
  bytes.insert(bytes.end(), m_options.hostKey->begin(), m_options.hostKey->end());
  bytes.push_back(m_options.hostOpen ? kFlagHostOpen : 0);
  std::uint8_t raw[16]{};
  if (Same(m_hostAt, m_relay)) {
    bytes.push_back(0); // the session is where we ask
  } else {
    bytes.push_back(4);
    std::memcpy(raw, &m_relay.host, 4); // network order
  }
  bytes.insert(bytes.end(), std::begin(raw), std::end(raw));
  AppendBe16(bytes, static_cast<std::uint16_t>(Same(m_hostAt, m_relay) ? 0 : m_relay.port));
  const auto padded = Padded(std::move(bytes));
  SendTo(m_outer, m_hostAt, padded.data(), padded.size());
  m_hostBoundMs = now;
  m_hostBinding = true;
}

void RelayLink::OnHostReply(const std::uint8_t *data, std::size_t size, enet_uint32 now) {
  if (size < kHeader + 1 || std::memcmp(data, kMagic, 4) != 0)
    return;
  const auto *body = data + kHeader;
  const auto length = size - kHeader;
  if (data[4] == kHostBound && length == 1 && body[0] <= 2) {
    m_status.hostBound = static_cast<HostPresence>(body[0]);
  } else if (data[4] == kRedirect && length == 1 + 4 + 2 && body[0] == 4 &&
             m_hostRedirects < kMaxRedirects) {
    std::memcpy(&m_hostAt.host, body + 1, 4); // network order already
    m_hostAt.port = Be16(body + 5);
    ++m_hostRedirects;
    BindHostId(now);
  } else if (data[4] == kError) {
    m_status.hostBound = HostPresence::Unknown;
  }
}

// A replica sends us to the one holding our code or session, or taking new
// sessions while it shuts down. One hop at most: replicas that disagree on
// who owns what cannot send us round in circles.
void RelayLink::OnRedirect(const std::uint8_t *body, std::size_t length, enet_uint32 now) {
  if (m_phase != Phase::Resolving && m_phase != Phase::Registering)
    return;
  if (m_redirects >= kMaxRedirects) {
    // Asking for a kept code, whose replica may be away: any code will do.
    if (m_role == Role::Host && m_options.keepCode && !m_keepFailed) {
      m_keepFailed = true;
      StartOver(now);
      return;
    }
    Fail("the relay's replicas sent us on twice (their peers lists disagree)");
    return;
  }
  if (length != 1 + 4 + 2 || body[0] != 4) {
    Fail("the relay sent us to an address we cannot reach (IPv6)");
    return;
  }
  ENetAddress to{};
  std::uint32_t ip{};
  std::memcpy(&ip, body + 1, 4); // network order already
  to.host = ip;
  to.port = Be16(body + 5);
  m_relay = to;
  ++m_redirects;
  m_solving = m_solved = false; // another replica, another challenge
  SendRequest(now);
}

// Our addresses for direct paths: the LAN one (the interface that reaches
// the relay, with the port of the socket we use for it); the relay adds the
// public one it sees. A guest's asks for an introduction to its host.
void RelayLink::SendAddresses(enet_uint32 now) {
  ENetAddress lan{};
  ENetSocket probe = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (probe != ENET_SOCKET_NULL) {
    if (enet_socket_connect(probe, &m_relay) < 0 || enet_socket_get_address(probe, &lan) < 0)
      lan.host = 0;
    enet_socket_destroy(probe);
  }
  ENetAddress outer{};
  enet_socket_get_address(m_outer, &outer);
  auto bytes = Header(kAddresses);
  bytes.push_back(static_cast<std::uint8_t>(Mapping())); // relay.FlagMapping*
  if (lan.host != 0 && outer.port != 0) {
    bytes.push_back(1);
    const auto *raw = reinterpret_cast<const std::uint8_t *>(&lan.host); // network order
    bytes.insert(bytes.end(), raw, raw + 4);
    AppendBe16(bytes, outer.port);
  } else {
    bytes.push_back(0);
  }
  ToRelay(Padded(std::move(bytes)), now);
  m_addressesMs = now;
  m_addressesSent = true;
  m_introduceAgainMs = now + kIntroduceAgainMs;
}

// The relay introduced us to the other side: punch at its public and LAN
// addresses (the nonce, drawn by the relay for the pair, says it is us).
void RelayLink::OnIntroduce(const std::uint8_t *body, std::size_t length, enet_uint32 now) {
  if (length != 2 + 4 + 1 + 6 + 6)
    return;
  // Both behind NATs that map each destination apart: punches cannot meet.
  if (body[6] == static_cast<std::uint8_t>(NatMapping::Varies) && Mapping() == NatMapping::Varies)
    return;
  const auto channel = Be16(body);
  Direct *direct = &m_direct;
  if (m_role == Role::Host) {
    auto *found = ChannelFor(channel, now);
    if (!found)
      return;
    direct = &found->direct;
  }
  *direct = Direct{};
  direct->nonce = Be32(body + 2);
  for (const auto *at : {body + 7, body + 13}) {
    const auto address = AddressAt(at);
    if (address.host == 0 || address.port == 0 ||
        (direct->count == 1 && Same(address, direct->candidates[0])))
      continue;
    direct->candidates[direct->count++] = address;
  }
  if (direct->nonce == 0 || direct->count == 0) {
    *direct = Direct{};
    return;
  }
  direct->punchUntilMs = now + m_options.times.punchMs;
  direct->punchedMs = now - kPunchEveryMs;
  DirectTimers(*direct, m_role == Role::Host ? channel : m_channel, now);
}

// A datagram from someone other than the relay: a punch of the other side
// (its nonce), or its data on a path a punch opened. Anything else is
// dropped.
void RelayLink::OnDirect(const ENetAddress &from, std::uint8_t *data, std::size_t size,
                         enet_uint32 now) {
  if (size < kHeader || std::memcmp(data, kMagic, 4) != 0)
    return;
  if (data[4] == kPunch && size == kPunchSize) {
    const auto nonce = Be32(data + kHeader);
    const auto channel = Be16(data + kHeader + 4);
    Direct *direct = nullptr;
    if (m_role == Role::Guest) {
      if (m_direct.nonce == nonce)
        direct = &m_direct;
    } else if (const auto found = m_channels.find(channel);
               found != m_channels.end() && found->second.direct.nonce == nonce) {
      direct = &found->second.direct;
    }
    if (!direct || nonce == 0)
      return;
    direct->to = from;
    direct->answered = true;
    direct->heardMs = now;
    if (data[kHeader + 6] == 0) {
      const auto ack = PunchDatagram(nonce, channel, true);
      SendTo(m_outer, from, ack.data(), ack.size());
    } else if (!direct->up) {
      direct->up = true; // our punch came back: both ways work
      direct->sentMs = now;
    }
    return;
  }
  if (data[4] != kData || size < kDataHeader)
    return;
  // Data on a path a punch opened (the other side sends on it once our
  // answer reached it: it works both ways).
  if (m_role == Role::Guest) {
    if (!m_direct.answered || !Same(from, m_direct.to))
      return;
    if (!m_direct.up)
      m_direct.sentMs = now;
    m_direct.up = true;
    m_direct.heardMs = now;
    SendTo(m_inner, m_enetSource, data + kDataHeader, size - kDataHeader);
    return;
  }
  for (auto &[id, channel] : m_channels) {
    auto &direct = channel.direct;
    if (!direct.answered || !Same(from, direct.to))
      continue;
    if (!direct.up)
      direct.sentMs = now;
    direct.up = true;
    direct.heardMs = now;
    SendTo(channel.socket, m_enetListen, data + kDataHeader, size - kDataHeader);
    channel.lastMs = now;
    return;
  }
}

void RelayLink::Forward(Direct &direct, std::uint8_t *buffer, std::size_t size, enet_uint32 now) {
  if (direct.up) {
    SendTo(m_outer, direct.to, buffer, size);
    direct.sentMs = now;
    return;
  }
  SendTo(m_outer, m_relay, buffer, size);
  m_sentMs = now;
}

// Punches while the introduction is fresh; a path that went silent while we
// sent on it is dropped, its traffic back on the relay at once.
void RelayLink::DirectTimers(Direct &direct, std::uint16_t channel, enet_uint32 now) {
  if (direct.nonce == 0)
    return;
  if (direct.up) {
    if (Since(now, direct.heardMs) >= m_options.times.directSilentMs &&
        Since(now, direct.sentMs) < m_options.times.directSilentMs) {
      direct = Direct{};
      if (m_role == Role::Guest)
        m_introduceAgainMs = now + kIntroduceAgainMs;
    }
    return;
  }
  if (Reached(now, direct.punchUntilMs)) {
    direct = Direct{}; // no answer: the relay it stays
    return;
  }
  if (Since(now, direct.punchedMs) < kPunchEveryMs)
    return;
  direct.punchedMs = now;
  const auto punch = PunchDatagram(direct.nonce, channel, false);
  for (int i = 0; i < direct.count; ++i)
    SendTo(m_outer, direct.candidates[i], punch.data(), punch.size());
  // Heard from an address we did not know of (its NAT mapped another
  // port): punched there too.
  if (direct.answered && !Same(direct.to, direct.candidates[0]) &&
      (direct.count < 2 || !Same(direct.to, direct.candidates[1])))
    SendTo(m_outer, direct.to, punch.data(), punch.size());
}

void RelayLink::SetDirect(bool on) {
  if (m_directOn == on)
    return;
  m_directOn = on;
  m_direct = Direct{};
  for (auto &[id, channel] : m_channels)
    channel.direct = Direct{};
  // On again: a guest asks for an introduction soon.
  m_introduceAgainMs = enet_time_get();
}

void RelayLink::OnError(std::uint8_t code) {
  switch (code) {
  case 1:
    Fail("the relay refused our host token (relay_token)");
    break;
  case 2:
    // A secret from an earlier session the host has left: its code may
    // name the host's new one.
    if (m_role == Role::Guest && m_secretGiven && NormalizeJoinCode(m_options.code)) {
      m_secretGiven = m_haveSecret = false;
      m_options.code = *NormalizeJoinCode(m_options.code);
      m_phase = Phase::Resolving;
      m_relay = m_home;
      m_redirects = 0;
      SendRequest(enet_time_get());
    } else {
      Fail("the session is not on the relay (the host left)");
    }
    break;
  case 3:
    Fail("the session is full");
    break;
  case 4:
    Fail("the relay has too many sessions open");
    break;
  case 5:
    Fail("another host holds this session on the relay");
    break;
  case 6:
    // Another relay configured may hold it (one of another network).
    if (!NextHome(enet_time_get()))
      Fail("no session has this join code (wrong, or expired)");
    break;
  case 7:
    Fail("too many join attempts: try again in a minute");
    break;
  case 8:
    Fail("the relay is restarting: try again in a moment");
    break;
  case 9:
    Fail("the relay hosts as many sessions from your address as it may");
    break;
  default:
    Fail("the relay refused us (error " + std::to_string(code) + ")");
    break;
  }
}

RelayLink::Channel *RelayLink::ChannelFor(std::uint16_t id, enet_uint32 now) {
  if (const auto found = m_channels.find(id); found != m_channels.end())
    return &found->second;
  if (m_channels.size() >= kMaxChannels) {
    // The one silent the longest makes room.
    const auto oldest = std::ranges::max_element(
        m_channels, {}, [&](const auto &entry) { return Since(now, entry.second.lastMs); });
    Close(oldest->second.socket);
    m_channels.erase(oldest);
  }
  ENetAddress local{};
  local.host = Loopback();
  auto socket = OpenSocket(&local);
  if (socket == ENET_SOCKET_NULL)
    return nullptr;
  if (enet_socket_get_address(socket, &local) < 0) {
    Close(socket);
    return nullptr;
  }
  return &m_channels.emplace(id, Channel{socket, now, local.port}).first->second;
}

bool RelayLink::Carries(const ENetAddress &address) const {
  if (address.host != Loopback())
    return false;
  if (m_role == Role::Guest)
    return Same(address, m_innerAddress);
  return std::ranges::any_of(m_channels,
                             [&](const auto &entry) { return entry.second.port == address.port; });
}

void RelayLink::Timers(enet_uint32 now) {
  // Choosing the nearest relay: probes until all answered or a second passed.
  if (m_phase == Phase::Choosing) {
    if (std::ranges::all_of(m_rttMs, [](enet_uint32 rtt) { return rtt != 0; }) ||
        Since(now, m_chooseMs) >= m_options.times.chooseMs) {
      Choose(now);
    } else if (Since(now, m_probedMs) >= kProbeEveryMs) {
      m_probedMs = now;
      for (std::size_t i = 0; i < m_homes.size(); ++i)
        if (m_rttMs[i] == 0)
          Probe(m_homes[i]);
    }
    return;
  }

  // The connection check, once registered: two relay ports.
  if (m_phase == Phase::Ready && m_checkTries < kCheckTries && Mapping() == NatMapping::Unknown &&
      Since(now, m_checkMs) >= kCheckEveryMs) {
    ++m_checkTries;
    m_checkMs = now;
    Probe(m_relay);
    Probe(m_checkAlt);
  }

  // Direct paths: punches, and silent ones dropped.
  if (m_role == Role::Guest) {
    DirectTimers(m_direct, m_channel, now);
    if (m_directOn && m_phase == Phase::Ready && !m_lost && m_direct.nonce == 0 &&
        Reached(now, m_introduceAgainMs))
      SendAddresses(now);
  } else {
    for (auto &[id, channel] : m_channels)
      DirectTimers(channel.direct, id, now);
    if (m_directOn && m_phase == Phase::Ready && !m_lost &&
        (!m_addressesSent || Since(now, m_addressesMs) >= kRefreshMs))
      SendAddresses(now);
  }

  // Registered: a silent relay is asked again, then waited for (traffic on
  // a direct path does not go through it: it is asked as when idle).
  if (m_phase == Phase::Ready) {
    const bool relaying =
        m_role == Role::Guest
            ? !m_direct.up
            : std::ranges::any_of(m_channels, [&](const auto &entry) {
                return Since(now, entry.second.lastMs) < m_options.times.idleProbeMs &&
                       !entry.second.direct.up;
              });
    const auto probeMs = relaying ? m_options.times.probeMs : m_options.times.idleProbeMs;
    const auto silent = Since(now, m_heardMs);
    if (silent >= probeMs + m_options.times.lostAfterProbeMs && !m_lost) {
      m_lost = true;
      m_lostMs = now;
    }
    if (m_lost && Since(now, m_lostMs) >= m_options.times.holdMs) {
      if (m_role == Role::Guest) {
        // Still with the host directly: the session goes on without the
        // relay, which is asked again now and then.
        if (m_direct.up) {
          m_lostMs = now;
          return;
        }
        Fail("lost the relay");
        return;
      }
      m_status.error = "lost the relay";
      StartOver(now);
      return;
    }
    if (silent >= probeMs && Since(now, m_requestMs) >= kResendMs)
      SendRequest(now);
  }

  if (m_role == Role::Guest) {
    if (m_phase == Phase::Resolving || m_phase == Phase::Registering) {
      if (Since(now, m_startMs) >= m_timeoutMs)
        Fail("the relay did not answer");
      else if (Since(now, m_requestMs) >= kResendMs)
        SendRequest(now);
    } else if (m_phase == Phase::Ready && Since(now, m_sentMs) >= kKeepaliveMs) {
      ToRelay(Header(kKeepalive), now);
    }
    return;
  }

  // Host: direct guests do not need the relay, so it never gives up on it.
  if (m_phase == Phase::Registering) {
    if (Since(now, m_startMs) >= m_timeoutMs) {
      Fail("the relay did not answer");
      m_requestMs = now;
    } else if (Since(now, m_requestMs) >= kResendMs) {
      SendRequest(now);
    }
  } else if (Since(now, m_requestMs) >= kRefreshMs) {
    if (m_phase == Phase::Failed) {
      // The error stays shown until it works; from the configured address,
      // which sends us to a replica that takes us.
      const auto error = m_status.error;
      StartOver(now);
      m_status.error = error;
    } else {
      SendRequest(now);
    }
  }

  // Our host ID: bound once registered, then as often as the registration
  // (the relay forgets it HostIDTTL after).
  if (m_phase == Phase::Ready && !m_lost && m_options.hostKey &&
      (!m_hostBinding || Since(now, m_hostBoundMs) >= kRefreshMs))
    BindHostId(now);

  // The code: asked until given, then again as often as the registration
  // (its time left, or back from a relay that restarted).
  if (m_phase == Phase::Ready && !m_lost) {
    const bool expired = !m_codeKept && Reached(now, m_codeExpiresMs);
    const bool wanted =
        m_status.code.empty() || expired || Since(now, m_codeRequestMs) >= kRefreshMs;
    if (wanted && Since(now, m_codeRequestMs) >= kCodeRetryMs)
      RequestCode(now);
  }

  for (auto it = m_channels.begin(); it != m_channels.end();) {
    if (Since(now, it->second.lastMs) >= kChannelIdleMs) {
      Close(it->second.socket);
      it = m_channels.erase(it);
    } else {
      ++it;
    }
  }
}

void RelayLink::Wait(ENetSocket enet, enet_uint32 ms) {
  ENetSocketSet set;
  ENET_SOCKETSET_EMPTY(set);
  ENetSocket highest = enet;
  ENET_SOCKETSET_ADD(set, enet);
  const auto add = [&](ENetSocket socket) {
    if (socket == ENET_SOCKET_NULL)
      return;
    ENET_SOCKETSET_ADD(set, socket);
    if (socket > highest) // not std::max: windows.h has a max macro
      highest = socket;
  };
  add(m_outer);
  add(m_inner);
  for (const auto &[id, channel] : m_channels)
    add(channel.socket);
  enet_socketset_select(highest, &set, nullptr, ms);
}

RelayStatus RelayLink::Status() const {
  auto status = m_status;
  switch (m_phase) {
  case Phase::Choosing:
  case Phase::Resolving:
  case Phase::Registering:
    status.state = RelayStatus::State::Connecting;
    break;
  case Phase::Ready:
    status.state = m_lost ? RelayStatus::State::Connecting : RelayStatus::State::Ready;
    if (m_lost)
      status.error = "the relay went silent: reconnecting";
    break;
  case Phase::Failed:
    status.state = RelayStatus::State::Failed;
    break;
  }
  status.codeKept = m_codeKept && !status.code.empty();
  status.codeSalt = m_options.codeSalt;
  if (!status.code.empty() && !m_codeKept) {
    const auto left = static_cast<std::int32_t>(m_codeExpiresMs - enet_time_get());
    status.codeSecondsLeft = left > 0 ? static_cast<std::uint32_t>(left) / 1000 : 0;
  }
  if (m_relay.host != 0)
    status.at = Text(m_relay);
  status.direct = m_direct.up ? 1 : 0;
  for (const auto &[id, channel] : m_channels)
    status.direct += channel.direct.up ? 1 : 0;
  // The connection check.
  if (const auto seen =
          std::ranges::find_if(m_seen, [&](const Seen &s) { return Same(s.relay, m_relay); });
      seen != m_seen.end() || !m_seen.empty()) {
    const auto &us = seen != m_seen.end() ? seen->us : m_seen[0].us;
    ENetAddress local{};
    enet_socket_get_address(m_outer, &local);
    status.publicAddress = Text(us);
    status.portKept = us.port == local.port;
  }
  status.nat = Mapping();
  return status;
}

std::optional<RelaySecret> RelayLink::Secret() const {
  if (m_role != Role::Guest || !m_haveSecret)
    return std::nullopt;
  return m_secret;
}
} // namespace tandem::net
