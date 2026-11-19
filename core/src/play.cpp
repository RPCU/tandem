#include "tandem/play.hpp"

#include "tandem/endpoint.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace tandem::play {
namespace {
template <class T> void Put(std::ofstream &out, const T &value) {
  out.write(reinterpret_cast<const char *>(&value), sizeof value);
}
template <class T> bool Get(std::ifstream &in, T &value) {
  return bool(in.read(reinterpret_cast<char *>(&value), sizeof value));
}

void Shift(proto::Vec3 &v, const proto::Vec3 &by) {
  v.x += by.x;
  v.y += by.y;
  v.z += by.z;
}

template <class M>
constexpr bool kKept =
    std::is_same_v<M, proto::PlayerState> || std::is_same_v<M, proto::PoseState> ||
    std::is_same_v<M, proto::Appearance> || std::is_same_v<M, proto::Held> ||
    std::is_same_v<M, proto::Shot> || std::is_same_v<M, proto::Seat> ||
    std::is_same_v<M, proto::VehicleState> || std::is_same_v<M, proto::VehicleRadio> ||
    std::is_same_v<M, proto::NpcHit> || std::is_same_v<M, proto::NpcEffect> ||
    std::is_same_v<M, proto::Stealth> || std::is_same_v<M, proto::Noise> ||
    std::is_same_v<M, proto::NpcHack> || std::is_same_v<M, proto::VehicleHack> ||
    std::is_same_v<M, proto::TimeEffect> || std::is_same_v<M, proto::NpcAction> ||
    std::is_same_v<M, proto::Vitals> || std::is_same_v<M, proto::Revive> ||
    std::is_same_v<M, proto::NpcStrike> || std::is_same_v<M, proto::AreaHit> ||
    std::is_same_v<M, proto::Transit> || std::is_same_v<M, proto::ShotHit> ||
    std::is_same_v<M, proto::Elsewhere>;
} // namespace

bool Keeps(const proto::Message &message) {
  return std::visit([](const auto &m) { return kKept<std::decay_t<decltype(m)>>; }, message);
}

std::optional<proto::PlayerState> Recording::FirstState() const {
  for (const auto &entry : entries)
    if (const auto *state = std::get_if<proto::PlayerState>(&entry.message))
      return *state;
  return std::nullopt;
}

std::size_t Recording::Count(std::size_t messageIndex) const {
  return static_cast<std::size_t>(std::ranges::count_if(
      entries, [&](const Entry &entry) { return entry.message.index() == messageIndex; }));
}

bool Writer::Open(const std::filesystem::path &path, const Header &header, std::string &error) {
  Close();
  std::error_code ec;
  if (path.has_parent_path())
    std::filesystem::create_directories(path.parent_path(), ec);
  m_out.open(path, std::ios::binary | std::ios::trunc);
  if (!m_out) {
    error = "cannot write " + path.string();
    return false;
  }
  m_path = path;
  m_header = header;
  m_count = 0;
  m_lastUs = 0;
  Put(m_out, kMagic);
  Put(m_out, header.version);
  Put(m_out, header.protocol);
  Put(m_out, header.startHostUs);
  Put(m_out, header.player);
  Put(m_out, static_cast<std::uint8_t>(header.body));
  return true;
}

void Writer::Add(std::uint64_t hostUs, const proto::Message &message) {
  if (!m_out.is_open())
    return;
  const auto bytes = proto::Encode(message);
  // Never back in time: the file is read in order.
  const std::uint64_t at =
      std::max(m_lastUs, hostUs > m_header.startHostUs ? hostUs - m_header.startHostUs : 0);
  const auto size = static_cast<std::uint32_t>(bytes.size());
  Put(m_out, at);
  Put(m_out, size);
  m_out.write(reinterpret_cast<const char *>(bytes.data()), size);
  m_lastUs = at;
  ++m_count;
}

void Writer::Close() {
  if (m_out.is_open())
    m_out.close();
}

std::optional<Recording> Load(const std::filesystem::path &path, std::string &error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read " + path.string();
    return std::nullopt;
  }
  Recording recording;
  std::uint32_t magic = 0;
  std::uint8_t body = 0;
  auto &h = recording.header;
  if (!Get(in, magic) || magic != kMagic || !Get(in, h.version) || !Get(in, h.protocol) ||
      !Get(in, h.startHostUs) || !Get(in, h.player) || !Get(in, body)) {
    error = path.string() + ": not a gameplay recording";
    return std::nullopt;
  }
  if (h.version != kVersion) {
    error = path.string() + ": recording version " + std::to_string(h.version);
    return std::nullopt;
  }
  h.body = static_cast<proto::Body>(body);
  for (;;) {
    std::uint64_t at = 0;
    std::uint32_t size = 0;
    if (!Get(in, at) || !Get(in, size) || size > (1u << 20))
      break;
    std::vector<std::byte> bytes(size);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), size))
      break;
    if (auto message = proto::Decode(bytes))
      recording.entries.push_back({at, std::move(*message)});
    else
      ++recording.skipped;
  }
  return recording;
}

std::optional<proto::Message> Adapt(proto::Message message, const Mapping &mapping,
                                    std::int64_t shiftUs) {
  const auto npc = [&](proto::NpcId &id, const proto::Vec3 *near) {
    if (!mapping.npc)
      return false;
    const auto mapped = mapping.npc(id, near);
    if (!mapped)
      return false;
    id = *mapped;
    return true;
  };
  const auto other = [&](proto::PlayerId &id) {
    if (id == mapping.self || id == proto::kNoPlayer)
      return true; // ours, or nobody's: the endpoint fills in who we are
    if (mapping.other == proto::kNoPlayer)
      return false;
    id = mapping.other;
    return true;
  };
  const bool keep = std::visit(
      [&](auto &m) -> bool {
        using M = std::decay_t<decltype(m)>;
        if constexpr (!kKept<M>)
          return false;
        else if constexpr (std::is_same_v<M, proto::PlayerState> ||
                           std::is_same_v<M, proto::VehicleState>) {
          Shift(m.position, mapping.offset);
          return true;
        } else if constexpr (std::is_same_v<M, proto::NpcHit> ||
                             std::is_same_v<M, proto::NpcStrike>) {
          Shift(m.position, mapping.offset);
          return npc(m.npc, &m.position);
        } else if constexpr (std::is_same_v<M, proto::ShotHit>) {
          Shift(m.position, mapping.offset);
          return npc(m.npc, &m.position);
        } else if constexpr (std::is_same_v<M, proto::NpcEffect> ||
                             std::is_same_v<M, proto::NpcHack> ||
                             std::is_same_v<M, proto::NpcAction>) {
          return npc(m.npc, nullptr);
        } else if constexpr (std::is_same_v<M, proto::AreaHit>) {
          Shift(m.position, mapping.offset);
          return m.player != mapping.self && other(m.player) && m.player != proto::kNoPlayer;
        } else if constexpr (std::is_same_v<M, proto::Seat>) {
          // Our own car (owner us): our copy of it again; theirs: the other's.
          if (m.owner == mapping.self)
            m.owner = proto::kNoPlayer;
          return other(m.owner);
        } else if constexpr (std::is_same_v<M, proto::VehicleHack>) {
          return m.driver != mapping.self && other(m.driver) && m.driver != proto::kNoPlayer;
        } else if constexpr (std::is_same_v<M, proto::Revive>) {
          return m.target != mapping.self && other(m.target) && m.target != proto::kNoPlayer;
        } else if constexpr (std::is_same_v<M, proto::VehicleRadio>) {
          if (m.trackStartUs)
            m.trackStartUs =
                static_cast<std::uint64_t>(static_cast<std::int64_t>(m.trackStartUs) + shiftUs);
          return true;
        } else if constexpr (std::is_same_v<M, proto::Transit>) {
          // A ride names a vehicle of the recorder's world: only the metro
          // (the same train everywhere) and getting off replay.
          if (m.kind == proto::TransitKind::None || m.kind == proto::TransitKind::Metro)
            return true;
          if (m.kind == proto::TransitKind::Along && other(m.carrier)) {
            m.vehicle = 0;
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<M, proto::Elsewhere>) {
          Shift(m.anchor, mapping.offset);
          return true;
        } else {
          return true;
        }
      },
      message);
  if (!keep)
    return std::nullopt;
  return message;
}

void Apply(Endpoint &endpoint, const proto::Message &message) {
  std::visit(
      [&](const auto &m) {
        using M = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<M, proto::PlayerState>)
          endpoint.SendLocalState(m);
        else if constexpr (std::is_same_v<M, proto::PoseState>)
          endpoint.SendLocalPose(m);
        else if constexpr (std::is_same_v<M, proto::Appearance>)
          endpoint.SetLocalAppearance(m);
        else if constexpr (std::is_same_v<M, proto::Held>)
          endpoint.SetLocalHeld(m);
        else if constexpr (std::is_same_v<M, proto::Shot>)
          endpoint.SendLocalShot(m);
        else if constexpr (std::is_same_v<M, proto::Seat>)
          endpoint.SetLocalSeat(m);
        else if constexpr (std::is_same_v<M, proto::VehicleState>)
          endpoint.SendLocalVehicle(m);
        else if constexpr (std::is_same_v<M, proto::VehicleRadio>)
          endpoint.SetLocalVehicleRadio(m);
        else if constexpr (std::is_same_v<M, proto::NpcHit>)
          endpoint.SendNpcHit(m);
        else if constexpr (std::is_same_v<M, proto::NpcEffect>)
          endpoint.SendNpcEffect(m);
        else if constexpr (std::is_same_v<M, proto::Stealth>)
          endpoint.SetLocalStealth(m);
        else if constexpr (std::is_same_v<M, proto::Noise>)
          endpoint.SendNoise(m);
        else if constexpr (std::is_same_v<M, proto::NpcHack>)
          endpoint.SendNpcHack(m);
        else if constexpr (std::is_same_v<M, proto::VehicleHack>)
          endpoint.SendVehicleHack(m);
        else if constexpr (std::is_same_v<M, proto::TimeEffect>)
          endpoint.SetLocalTimeEffect(m);
        else if constexpr (std::is_same_v<M, proto::NpcAction>)
          endpoint.SendNpcAction(m);
        else if constexpr (std::is_same_v<M, proto::Vitals>)
          endpoint.SetLocalVitals(m);
        else if constexpr (std::is_same_v<M, proto::Revive>)
          endpoint.SendRevive(m);
        else if constexpr (std::is_same_v<M, proto::NpcStrike> || std::is_same_v<M, proto::AreaHit>)
          endpoint.SendCombat(m);
        else if constexpr (std::is_same_v<M, proto::Transit>)
          endpoint.SetLocalTransit(m);
        else if constexpr (std::is_same_v<M, proto::ShotHit>)
          endpoint.SendShotHit(m);
        else if constexpr (std::is_same_v<M, proto::Elsewhere>)
          endpoint.SetLocalElsewhere(m);
      },
      message);
}

Player::Player(Recording recording, bool loop, std::uint64_t lapGapUs)
    : m_recording(std::move(recording)), m_loop(loop), m_lapGapUs(lapGapUs) {
  m_done = m_recording.entries.empty();
}

std::vector<const Entry *> Player::Due(std::uint64_t elapsedUs) {
  std::vector<const Entry *> due;
  const auto &entries = m_recording.entries;
  while (!m_done) {
    if (m_next == entries.size()) {
      if (!m_loop) {
        m_done = true;
        break;
      }
      const auto nextLap = m_lapStartUs + m_recording.DurationUs() + m_lapGapUs;
      if (elapsedUs < nextLap)
        break;
      m_lapStartUs = nextLap;
      m_next = 0;
      ++m_lap;
    }
    if (m_lapStartUs + entries[m_next].atUs > elapsedUs)
      break;
    due.push_back(&entries[m_next++]);
  }
  return due;
}
} // namespace tandem::play
