#include "relay_link.hpp"
#include "resolve.hpp"

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
};

constexpr enet_uint32 kResendMs = 500;
// Host: registration and code asked again this often, so a relay that
// restarted (and forgot us) has the session back within it.
constexpr enet_uint32 kRefreshMs = 20'000;
constexpr enet_uint32 kKeepaliveMs = 10'000; // the relay forgets a client after 2 min
constexpr enet_uint32 kCodeRetryMs = 1'000;
// Registered: a relay silent this long is asked again (our registration,
// answered at once); silent this long, it is lost and the session holds
// its relayed players. A session's ENet pings each way every half second,
// so a live relay is not silent for a second.
// A host with no relayed guest hears only the answers to its probes: it
// asks every few seconds (its code comes back as soon after a restart).
constexpr enet_uint32 kProbeMs = 1'000;
constexpr enet_uint32 kIdleProbeMs = 5'000;
constexpr enet_uint32 kLostAfterProbeMs = 2'000;
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
    : m_role(role), m_options(std::move(options)), m_timeoutMs(timeoutMs),
      m_enetSource(enetSource) {
  m_startMs = enet_time_get();
  if (!ResolveIpv4(m_options.address, m_relay)) {
    Fail("unknown relay " + m_options.address);
    return;
  }
  m_relay.port = m_options.port;
  m_home = m_relay;
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
    std::random_device random;
    for (std::size_t i = 0; i < m_secret.size(); i += 4) {
      const auto word = random();
      for (std::size_t j = 0; j < 4; ++j)
        m_secret[i + j] = static_cast<std::uint8_t>(word >> (8 * j));
    }
    m_haveSecret = true;
    m_phase = Phase::Registering;
  }
  SendRequest(m_startMs);
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
}

void RelayLink::ToRelay(const std::vector<std::uint8_t> &bytes, enet_uint32 now) {
  SendTo(m_outer, m_relay, bytes.data(), bytes.size());
  m_sentMs = now;
}

void RelayLink::SendRequest(enet_uint32 now) {
  std::vector<std::uint8_t> bytes;
  if (m_phase == Phase::Resolving) {
    bytes = Header(kResolveCode);
    bytes.insert(bytes.end(), m_options.code.begin(), m_options.code.end());
  } else if (m_role == Role::Host) {
    bytes = Header(kRegisterHost);
    bytes.insert(bytes.end(), m_secret.begin(), m_secret.end());
    bytes.insert(bytes.end(), m_options.token.begin(), m_options.token.end());
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

void RelayLink::StartOver(enet_uint32 now) {
  m_relay = m_home;
  m_redirected = false;
  m_lost = false;
  m_phase = Phase::Registering;
  m_startMs = now;
  m_status.code.clear(); // that replica's: another gives another
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
    if (Same(from, m_relay))
      OnRelay(buffer, size, now);
  }

  if (m_role == Role::Guest && m_inner != ENET_SOCKET_NULL) {
    for (int i = 0; i < 256; ++i) {
      const auto size = ReceiveFrom(m_inner, from, buffer + kDataHeader, kMaxDatagram);
      if (size == 0)
        break;
      if (!Same(from, m_enetSource))
        continue; // only our own ENet speaks through the proxy
      WrapData(buffer, 0);
      if (m_phase == Phase::Ready) {
        SendTo(m_outer, m_relay, buffer, kDataHeader + size);
        m_sentMs = now;
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
      if (!Same(from, m_enetListen) || m_phase != Phase::Ready)
        continue;
      WrapData(buffer, id);
      SendTo(m_outer, m_relay, buffer, kDataHeader + size);
      m_sentMs = channel.lastMs = now;
    }
  }

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
    if (m_phase == Phase::Ready) {
      // Our probe answered: the relay is there (again). It may have
      // restarted, our code with it: asked for at once.
      m_lost = false;
      if (m_role == Role::Guest)
        m_channel = Be16(body);
      else if (quiet >= kProbeMs)
        m_codeRequestMs = now - kRefreshMs;
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
    break;
  case kError:
    // Registered, a refusal answers a probe of a relay that restarted and
    // does not have our session yet (its host is not back): asked again
    // until the hold ends.
    if (length == 1 && m_phase != Phase::Ready)
      OnError(body[0]);
    break;
  case kRedirect:
    OnRedirect(body, length, now);
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
    m_codeExpiresMs = now + 1000u * Be16(body + kJoinCodeSize);
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

// A replica sends us to the one holding our code or session, or taking new
// sessions while it shuts down. One hop at most: replicas that disagree on
// who owns what cannot send us round in circles.
void RelayLink::OnRedirect(const std::uint8_t *body, std::size_t length, enet_uint32 now) {
  if (m_phase != Phase::Resolving && m_phase != Phase::Registering)
    return;
  if (m_redirected) {
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
  m_redirected = true;
  SendRequest(now);
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
      m_redirected = false;
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
    Fail("no session has this join code (wrong, or expired)");
    break;
  case 7:
    Fail("too many join attempts: try again in a minute");
    break;
  case 8:
    Fail("the relay is restarting: try again in a moment");
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
  // Registered: a silent relay is asked again, then waited for.
  if (m_phase == Phase::Ready) {
    const bool relaying =
        m_role == Role::Guest || std::ranges::any_of(m_channels, [&](const auto &entry) {
          return Since(now, entry.second.lastMs) < kIdleProbeMs;
        });
    const auto probeMs = relaying ? kProbeMs : kIdleProbeMs;
    const auto silent = Since(now, m_heardMs);
    if (silent >= probeMs + kLostAfterProbeMs && !m_lost) {
      m_lost = true;
      m_lostMs = now;
    }
    if (m_lost && Since(now, m_lostMs) >= kHoldMs) {
      if (m_role == Role::Guest) {
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

  // The code: asked until given, then again as often as the registration
  // (its time left, or back from a relay that restarted).
  if (m_phase == Phase::Ready && !m_lost) {
    const bool wanted = m_status.code.empty() || Reached(now, m_codeExpiresMs) ||
                        Since(now, m_codeRequestMs) >= kRefreshMs;
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
  if (!status.code.empty()) {
    const auto left = static_cast<std::int32_t>(m_codeExpiresMs - enet_time_get());
    status.codeSecondsLeft = left > 0 ? static_cast<std::uint32_t>(left) / 1000 : 0;
  }
  if (m_relay.host != 0)
    status.at = Text(m_relay);
  return status;
}

std::optional<RelaySecret> RelayLink::Secret() const {
  if (m_role != Role::Guest || !m_haveSecret)
    return std::nullopt;
  return m_secret;
}
} // namespace tandem::net
