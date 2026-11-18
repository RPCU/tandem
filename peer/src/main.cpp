// tandem-peer: a stand-in second player on Linux. It speaks the real protocol
// through the same core as the game plugin, so one copy of the game can be
// tested against it.
//
//   tandem-peer host [options]          the game joins it
//   tandem-peer join ADDRESS[:PORT]     it joins the game
//
// Modes (--mode):
//   mirror  copies your V back as a second player, offset and delayed: you see
//           your own movements on a puppet, through the whole network path
//   ghost   circles around your V
//   front   stands 3 m in front of your V, facing it, stepping side to side:
//           always in view, for checking how a puppet looks and moves
//   drive   drives a car in circles around your V (--vehicle RECORD): tests
//           the copy of a remote player's car, its driver and its motion;
//           --radio STATION plays that station on the car's radio, for
//           whoever rides in it
//   idle    only connects and reports
//
// Looks, held weapons, shots, seats and cars: mirror and front echo yours
// back (the car moved by --offset, so the copy does not sit in yours).
// Pause menu: --pause-at opens "its pause menu" that many seconds into the
// session (once another player is there) for --pause-for seconds; the pause
// states it hears are logged.
// Loot: --drop-item drops a shared world item in front of your V; --take-items
// picks up what the others drop; as the host of a replay, --remove-corpses
// takes the bodies of NPCs a guest killed away (their proxies' loot is left
// as a bag). The world items and pickups it hears are logged.
// Body pose: mirror and front echo your V's pose; --pose FILE (a .tpose made by
// tools/re/pose_export.py from a recording) plays a recorded pose instead, in
// a loop, with any mode.

#include "tandem/endpoint.hpp"
#include "tandem/interpolation.hpp"
#include "tandem/pose.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <deque>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using tandem::proto::PlayerState;
using tandem::proto::Vec3;

volatile std::sig_atomic_t g_stop = 0;

enum class Mode { Mirror, Ghost, Front, Drive, Idle };

struct Options {
  bool host{};
  std::string address{"127.0.0.1"};
  std::uint16_t port{tandem::proto::kDefaultPort};
  Mode mode{Mode::Mirror};
  std::string name{"tandem-peer"};
  tandem::proto::Body body{tandem::proto::Body::Female};
  Vec3 offset{2.f, 0.f, 0.f};
  std::chrono::milliseconds delay{0};
  int rateHz{30};
  std::string poseFile;
  std::string vehicle{"Vehicle.v_standard2_archer_hella_player"};
  int driveSeconds{0}; // drive: get out after this long (0: never)
  // drive: the car's radio (ERadioStationList index, -1 off); unset: none sent
  std::optional<int> radioStation;
  int hitNpcsSeconds{0}; // join: hit one of the host's NPCs this often (0: never)
  // host: the world the game follows (time of day, weather name); unset: none sent
  std::optional<int> timeOfDayMinutes;
  std::string weather;
  std::string recordFile; // join: save the host's NPC messages here
  std::string replayFile; // host: send a recording's NPC messages again
  int pauseAtSeconds{-1}; // open the pause menu this long into the session (-1: never)
  int pauseForSeconds{3}; // ...for this long
  std::string dropItem;   // drop one of this record in front of the other V (empty: never)
  int takeItemsSeconds{
      -1}; // pick up the others' world items this long after they appear (-1: never)
  int removeCorpsesSeconds{
      -1};      // host of a replay: dead NPCs' bodies go after this long (-1: never)
  bool check{}; // parse the options, then exit
};

// TweakDBID of a record name: CRC32 of the name, its length in the top byte.
std::uint64_t TweakDBID(std::string_view name) {
  std::uint32_t crc = 0xFFFFFFFF;
  for (const unsigned char c : name) {
    crc ^= c;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
  }
  return static_cast<std::uint64_t>(~crc) | (static_cast<std::uint64_t>(name.size()) << 32);
}

// CName hash: FNV-1a 64.
std::uint64_t CName(std::string_view name) {
  std::uint64_t hash = 0xCBF29CE484222325;
  for (const unsigned char c : name)
    hash = (hash ^ c) * 0x100000001B3;
  return hash;
}

// The game's radio stations, by ERadioStationList index (final.reds,
// RadioStationDataProvider.GetStationName).
constexpr std::string_view kStations[] = {
    "radio_station_02_aggro_ind",    "radio_station_03_elec_ind",  "radio_station_04_hiphop",
    "radio_station_07_aggro_techno", "radio_station_09_downtempo", "radio_station_01_att_rock",
    "radio_station_05_pop",          "radio_station_10_latino",    "radio_station_11_metal",
    "radio_station_06_minim_techno", "radio_station_08_jazz",      "radio_station_12_growl_fm",
    "radio_station_13_dark_star",    "radio_station_14_impulse_fm"};

std::string_view StationName(int station) {
  return station >= 0 && station < static_cast<int>(std::size(kStations)) ? kStations[station]
                                                                          : "off";
}

template <class... Args> void Log(std::format_string<Args...> format, Args &&...args) {
  std::cout << std::format("[{:>8.3f}] ", tandem::net::NowUs() % 100'000'000 / 1e6)
            << std::format(format, std::forward<Args>(args)...) << '\n'
            << std::flush;
}

[[noreturn]] void Usage(std::string_view error = {}) {
  if (!error.empty())
    std::cerr << "tandem-peer: " << error << "\n\n";
  std::cerr << "usage: tandem-peer host [options]\n"
               "       tandem-peer join ADDRESS[:PORT] [options]\n\n"
               "  --port N           UDP port (default 27450)\n"
               "  --mode M           mirror | ghost | front | drive | idle (default mirror)\n"
               "  --name NAME        player name (default tandem-peer)\n"
               "  --body B           female | male: the puppet rig others see (default female)\n"
               "  --offset X,Y,Z     mirror: metres added to your position (default 2,0,0)\n"
               "  --delay MS         mirror: extra delay before copying (default 0)\n"
               "  --rate HZ          states sent per second (default 30)\n"
               "  --pose FILE        play a recorded body pose (.tpose) in a loop\n"
               "  --vehicle RECORD   drive: the car's record (default "
               "Vehicle.v_standard2_archer_hella_player)\n"
               "  --drive-seconds N  drive: get out after N seconds and stand by the car\n"
               "  --radio STATION    drive: the car's radio plays STATION (0-13 or its name,\n"
               "                     radio_station_09_downtempo...; off): sent when seated and\n"
               "                     again when a passenger gets in\n"
               "  --hit-npcs N       join: hit one of the host's NPCs (35 health) every N seconds\n"
               "  --time HH:MM       host: the time of day the game must follow\n"
               "  --weather NAME     host: the weather it must follow (24h_weather_rain...)\n"
               "  --record FILE      join: save the host's NPCs (spawns, states, gone) to FILE\n"
               "  --replay FILE      host: send a recording's NPCs again, at its pace (moved by\n"
               "                     --offset)\n"
               "  --pause-at N       open the pause menu N seconds after another player is in the\n"
               "                     session: every game pauses\n"
               "  --pause-for N      --pause-at: close it again after N seconds (default 3)\n"
               "  --drop-item RECORD drop one RECORD (Items.FirstAidWhiffV0...) as a shared world\n"
               "                     item in front of the other player, 5 s after they are there\n"
               "  --take-items N     pick up every world item the others drop, N seconds after it\n"
               "                     appears (the host gives it to the first)\n"
               "  --remove-corpses N host of a replay: an NPC a guest killed is taken away N\n"
               "                     seconds after its death (its body gone: NpcGone reason 0)\n";
  std::exit(error.empty() ? 0 : 2);
}

template <class T> T Number(std::string_view text) {
  T value{};
  auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    Usage(std::format("not a number: {}", text));
  return value;
}

Options Parse(int argc, char **argv) {
  std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "--help" || args[0] == "-h")
    Usage();

  Options options;
  std::size_t i = 1;
  if (args[0] == "host")
    options.host = true;
  else if (args[0] == "join") {
    if (args.size() < 2)
      Usage("join needs an address");
    auto target = args[1];
    if (auto colon = target.rfind(':'); colon != std::string_view::npos) {
      options.port = Number<std::uint16_t>(target.substr(colon + 1));
      target = target.substr(0, colon);
    }
    options.address = std::string(target);
    i = 2;
  } else
    Usage(std::format("unknown command {}", args[0]));

  for (; i < args.size(); ++i) {
    const auto flag = args[i];
    if (flag == "--check") { // parse only (tests/peer_flags.sh)
      options.check = true;
      continue;
    }
    if (i + 1 >= args.size())
      Usage(std::format("{} needs a value", flag));
    const auto value = args[++i];
    if (flag == "--port")
      options.port = Number<std::uint16_t>(value);
    else if (flag == "--name")
      options.name = std::string(value);
    else if (flag == "--body") {
      if (value == "female")
        options.body = tandem::proto::Body::Female;
      else if (value == "male")
        options.body = tandem::proto::Body::Male;
      else
        Usage(std::format("unknown body {}", value));
    } else if (flag == "--delay")
      options.delay = std::chrono::milliseconds(Number<int>(value));
    else if (flag == "--time") {
      const auto colon = value.find(':');
      if (colon == std::string_view::npos)
        Usage("--time is HH:MM");
      options.timeOfDayMinutes =
          Number<int>(value.substr(0, colon)) * 60 + Number<int>(value.substr(colon + 1));
    } else if (flag == "--weather")
      options.weather = std::string(value);
    else if (flag == "--pause-at")
      options.pauseAtSeconds = Number<int>(value);
    else if (flag == "--pause-for")
      options.pauseForSeconds = Number<int>(value);
    else if (flag == "--drop-item")
      options.dropItem = std::string(value);
    else if (flag == "--take-items")
      options.takeItemsSeconds = Number<int>(value);
    else if (flag == "--remove-corpses")
      options.removeCorpsesSeconds = Number<int>(value);
    else if (flag == "--hit-npcs")
      options.hitNpcsSeconds = Number<int>(value);
    else if (flag == "--drive-seconds")
      options.driveSeconds = Number<int>(value);
    else if (flag == "--radio") {
      if (value == "off")
        options.radioStation = -1;
      else if (const auto *named = std::ranges::find(kStations, value);
               named != std::end(kStations))
        options.radioStation = static_cast<int>(named - std::begin(kStations));
      else if (const auto station = Number<int>(value); station >= 0 && station < 14)
        options.radioStation = station;
      else
        Usage(std::format("--radio: no station {}", value));
    } else if (flag == "--vehicle")
      options.vehicle = std::string(value);
    else if (flag == "--pose")
      options.poseFile = std::string(value);
    else if (flag == "--rate")
      options.rateHz = std::max(1, Number<int>(value));
    else if (flag == "--mode") {
      if (value == "mirror")
        options.mode = Mode::Mirror;
      else if (value == "ghost")
        options.mode = Mode::Ghost;
      else if (value == "front")
        options.mode = Mode::Front;
      else if (value == "idle")
        options.mode = Mode::Idle;
      else if (value == "drive")
        options.mode = Mode::Drive;
      else
        Usage(std::format("unknown mode {}", value));
    } else if (flag == "--offset") {
      float xyz[3]{};
      std::size_t start = 0;
      for (int axis = 0; axis < 3; ++axis) {
        const auto comma = value.find(',', start);
        if ((axis < 2) == (comma == std::string_view::npos))
          Usage("--offset is X,Y,Z");
        xyz[axis] = Number<float>(value.substr(start, comma - start));
        start = comma + 1;
      }
      options.offset = {xyz[0], xyz[1], xyz[2]};
    } else if (flag == "--record") {
      options.recordFile = value;
    } else if (flag == "--replay") {
      options.replayFile = value;
    } else
      Usage(std::format("unknown option {}", flag));
  }
  return options;
}

// A recorded pose, played in a loop (tools/re/pose_export.py's format).
class PoseClip {
public:
  bool Load(const std::string &path, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    char magic[4]{};
    std::uint32_t version = 0, frames = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char *>(&version), 4);
    in.read(reinterpret_cast<char *>(&frames), 4);
    in.read(reinterpret_cast<char *>(&m_referenceHips), 4);
    if (!in || std::string_view(magic, 4) != "TPOS" || version != 1 || frames == 0) {
      error = std::format("{}: not a pose file", path);
      return false;
    }
    m_frames.resize(frames);
    for (auto &frame : m_frames) {
      in.read(reinterpret_cast<char *>(&frame.time), 4);
      in.read(reinterpret_cast<char *>(&frame.hips), 12);
      in.read(reinterpret_cast<char *>(frame.model.data()), sizeof(frame.model));
    }
    if (!in) {
      error = std::format("{}: truncated", path);
      return false;
    }
    return true;
  }

  // The pose `nowUs` into the loop.
  tandem::proto::PoseState At(std::uint64_t nowUs) const {
    const float span = m_frames.back().time - m_frames.front().time;
    const float t = m_frames.front().time + std::fmod(static_cast<float>(nowUs / 1e6), span);
    std::size_t i = 0;
    while (i + 1 < m_frames.size() && m_frames[i + 1].time <= t)
      ++i;
    const auto &frame = m_frames[i];
    return tandem::pose::Pack(tandem::pose::FromModel(frame.model, frame.hips, m_referenceHips));
  }

  std::size_t Frames() const { return m_frames.size(); }
  float Seconds() const { return m_frames.back().time - m_frames.front().time; }

private:
  struct Frame {
    float time{};
    Vec3 hips;
    std::array<tandem::proto::Quat, tandem::pose::kBones> model{};
  };
  std::vector<Frame> m_frames;
  float m_referenceHips{1.f};
};

// NPC messages with their time, for --record / --replay: per message
// u64 microseconds since the first, u32 length, the encoded message.
class NpcRecording {
public:
  bool Open(const std::string &path) {
    m_out.open(path, std::ios::binary);
    return bool(m_out);
  }
  void Add(const tandem::proto::Message &message) {
    const auto now = tandem::net::NowUs();
    if (!m_start)
      m_start = now;
    const auto bytes = tandem::proto::Encode(message);
    const std::uint64_t at = now - *m_start;
    const auto size = static_cast<std::uint32_t>(bytes.size());
    m_out.write(reinterpret_cast<const char *>(&at), 8);
    m_out.write(reinterpret_cast<const char *>(&size), 4);
    m_out.write(reinterpret_cast<const char *>(bytes.data()), size);
    m_out.flush();
    ++m_count;
  }
  std::size_t Count() const { return m_count; }

  struct Entry {
    std::uint64_t atUs;
    tandem::proto::Message message;
  };
  static std::vector<Entry> Load(const std::string &path) {
    std::vector<Entry> out;
    std::ifstream in(path, std::ios::binary);
    for (;;) {
      std::uint64_t at = 0;
      std::uint32_t size = 0;
      if (!in.read(reinterpret_cast<char *>(&at), 8) ||
          !in.read(reinterpret_cast<char *>(&size), 4))
        break;
      std::vector<std::byte> bytes(size);
      if (!in.read(reinterpret_cast<char *>(bytes.data()), size))
        break;
      if (auto message = tandem::proto::Decode(bytes))
        out.push_back({at, std::move(*message)});
    }
    return out;
  }

private:
  std::ofstream m_out;
  std::optional<std::uint64_t> m_start;
  std::size_t m_count{};
};

// What the mock player does with the states it hears.
class Puppeteer {
public:
  explicit Puppeteer(const Options &options) : m_options(options) {}

  void Heard(const PlayerState &state) {
    m_heard.push_back({tandem::net::NowUs(), state});
    while (m_heard.size() > 512)
      m_heard.pop_front();
  }

  void HeardPose(const tandem::proto::PoseState &pose) {
    m_poses.push_back({tandem::net::NowUs(), pose});
    while (m_poses.size() > 256)
      m_poses.pop_front();
  }

  // The pose to send: the clip's, else (mirror, front) the newest heard at
  // least `delay` old.
  std::optional<tandem::proto::PoseState> NextPose(std::uint64_t nowUs, const PoseClip *clip) {
    if (clip)
      return clip->At(nowUs);
    // mirror and front show your own pose back (front: in view, facing you).
    if (m_options.mode != Mode::Mirror && m_options.mode != Mode::Front)
      return std::nullopt;
    const auto delayUs = static_cast<std::uint64_t>(m_options.delay.count()) * 1000;
    const tandem::proto::PoseState *pick = nullptr;
    for (const auto &[at, pose] : m_poses)
      if (at + delayUs <= nowUs)
        pick = &pose;
    if (!pick)
      return std::nullopt;
    return *pick;
  }

  std::optional<PlayerState> Next(std::uint64_t nowUs) {
    switch (m_options.mode) {
    case Mode::Idle:
      return std::nullopt;
    case Mode::Mirror:
      return Mirror(nowUs);
    case Mode::Ghost:
      return Ghost(nowUs);
    case Mode::Front:
      return Front(nowUs);
    case Mode::Drive:
      if (m_parkedAt)
        return Parked();
      return Drive(nowUs).first;
    }
    return std::nullopt;
  }

private:
  std::optional<PlayerState> Mirror(std::uint64_t nowUs) {
    // The newest state at least `delay` old.
    const auto delayUs = static_cast<std::uint64_t>(m_options.delay.count()) * 1000;
    const PlayerState *pick = nullptr;
    for (const auto &[at, state] : m_heard)
      if (at + delayUs <= nowUs)
        pick = &state;
    if (!pick)
      return std::nullopt;
    auto out = *pick;
    out.position.x += m_options.offset.x;
    out.position.y += m_options.offset.y;
    out.position.z += m_options.offset.z;
    return out;
  }

  std::optional<PlayerState> Ghost(std::uint64_t nowUs) {
    constexpr float kRadius = 3.f;
    constexpr float kSpeed = 2.f; // m/s along the circle
    const Vec3 center = m_heard.empty() ? Vec3{} : m_heard.back().second.position;
    const float angle = static_cast<float>(nowUs / 1e6) * kSpeed / kRadius;

    PlayerState out;
    out.position = {center.x + kRadius * std::cos(angle), center.y + kRadius * std::sin(angle),
                    center.z};
    out.velocity = {-kSpeed * std::sin(angle), kSpeed * std::cos(angle), 0.f};
    // Facing along the circle: yaw around Z (the game's up axis).
    const float yaw = angle + std::numbers::pi_v<float> / 2.f;
    out.orientation = {0.f, 0.f, std::sin(yaw / 2.f), std::cos(yaw / 2.f)};
    return out;
  }

public:
  // A car going round a 12 m circle at 8 m/s around where your V was first
  // heard (fixed, so you can watch it pass): the driver's state, and the car's.
  std::pair<std::optional<PlayerState>, std::optional<tandem::proto::VehicleState>>
  Drive(std::uint64_t nowUs) {
    if (m_heard.empty())
      return {};
    if (!m_center)
      m_center = m_heard.back().second.position;
    constexpr float kRadius = 12.f, kSpeed = 8.f;
    const float angle = static_cast<float>(nowUs / 1e6) * kSpeed / kRadius;
    const Vec3 at{m_center->x + kRadius * std::cos(angle), m_center->y + kRadius * std::sin(angle),
                  m_center->z};
    const Vec3 velocity{-kSpeed * std::sin(angle), kSpeed * std::cos(angle), 0.f};
    // The game's forward is +Y turned by yaw: facing along the circle.
    const auto facing = tandem::sync::FromYaw(angle);
    PlayerState body;
    body.position = at;
    body.velocity = velocity;
    body.orientation = facing;
    tandem::proto::VehicleState car;
    car.position = at;
    car.orientation = facing;
    car.velocity = velocity;
    return {body, car};
  }

  void Park(const Vec3 &at) { m_parkedAt = at; }

private:
  std::optional<Vec3> m_center;
  std::optional<Vec3> m_parkedAt;

  std::optional<PlayerState> Parked() const {
    PlayerState out;
    out.position = {m_parkedAt->x + 2.f, m_parkedAt->y, m_parkedAt->z};
    return out;
  }

  std::optional<PlayerState> Front(std::uint64_t nowUs) {
    if (m_heard.empty())
      return std::nullopt;
    constexpr float kAhead = 3.f, kSway = 2.f, kRate = 0.6f; // m, m, rad/s
    const auto &them = m_heard.back().second;
    const float yaw = tandem::sync::YawOf(them.orientation);
    // The game's forward is +Y turned by yaw: (-sin, cos); right is (cos, sin).
    const float fx = -std::sin(yaw), fy = std::cos(yaw);
    const float rx = std::cos(yaw), ry = std::sin(yaw);
    const float t = static_cast<float>(nowUs % 600'000'000ULL) / 1e6f;
    const float side = kSway * std::sin(t * kRate);
    const float sideSpeed = kSway * kRate * std::cos(t * kRate);

    PlayerState out;
    out.position = {them.position.x + fx * kAhead + rx * side,
                    them.position.y + fy * kAhead + ry * side, them.position.z};
    out.velocity = {rx * sideSpeed, ry * sideSpeed, 0.f};
    out.orientation = tandem::sync::FromYaw(yaw + std::numbers::pi_v<float>); // facing them
    // Crouched 4 s out of every 8 (gamePSMLocomotionStates::Crouch = 1).
    out.stance = (static_cast<int>(t) % 8) < 4 ? 1 : 0;
    return out;
  }

  const Options &m_options;
  std::deque<std::pair<std::uint64_t, PlayerState>> m_heard;
  std::deque<std::pair<std::uint64_t, tandem::proto::PoseState>> m_poses;
};
} // namespace

int main(int argc, char **argv) {
  const auto options = Parse(argc, argv);
  if (options.check) {
    std::cout << "options ok\n";
    return 0;
  }
  std::signal(SIGINT, [](int) { g_stop = 1; });
  std::signal(SIGTERM, [](int) { g_stop = 1; });

  std::string error;
  auto endpoint = options.host
                      ? tandem::Endpoint::Host({options.name, options.body}, options.port, error)
                      : tandem::Endpoint::Join({options.name, options.body}, options.address,
                                               options.port, error);
  if (!endpoint) {
    std::cerr << "tandem-peer: " << error << '\n';
    return 1;
  }
  if (options.host)
    Log("hosting on UDP {} as {}", options.port, options.name);
  else
    Log("joining {}:{} as {}", options.address, options.port, options.name);

  PoseClip clip;
  if (!options.poseFile.empty()) {
    if (!clip.Load(options.poseFile, error)) {
      std::cerr << "tandem-peer: " << error << '\n';
      return 1;
    }
    Log("pose: {} frames, {:.0f} s, in a loop", clip.Frames(), clip.Seconds());
  }
  const PoseClip *poseClip = options.poseFile.empty() ? nullptr : &clip;

  NpcRecording recording;
  if (!options.recordFile.empty() && !recording.Open(options.recordFile)) {
    std::cerr << "tandem-peer: cannot write " << options.recordFile << '\n';
    return 1;
  }
  std::set<tandem::proto::NpcId> liveNpcs;
  std::map<tandem::proto::NpcId, float> npcHealth; // host: replayed NPCs hit by guests
  std::set<tandem::proto::NpcId> deadNpcs;
  std::optional<tandem::proto::NpcId> hitTarget;
  std::set<tandem::proto::NpcId> burnt;
  std::map<tandem::proto::NpcId, std::uint8_t> detection;
  auto nextHit = std::chrono::steady_clock::now();
  std::vector<NpcRecording::Entry> replay;
  std::size_t replayNext = 0;
  std::optional<std::uint64_t> replayStartUs;
  if (!options.replayFile.empty()) {
    replay = NpcRecording::Load(options.replayFile);
    Log("replay: {} NPC messages from {}", replay.size(), options.replayFile);
  }

  Puppeteer puppeteer(options);
  const auto startUs = tandem::net::NowUs();
  auto nextWorld = std::chrono::steady_clock::now();
  bool seated = false, parked = false;
  std::uint64_t seatedAtUs = 0;
  std::optional<tandem::proto::VehicleState> lastCar;
  // --radio: the radio of the car we drive, as a driver's game sends it.
  const auto sendRadio = [&](std::string_view why) {
    const int station = *options.radioStation;
    endpoint->SetLocalVehicleRadio(
        {.on = station >= 0, .station = static_cast<std::int8_t>(station)});
    Log("car radio sent ({}): {}", why, StationName(station));
  };
  const auto interval = std::chrono::microseconds(1'000'000 / options.rateHz);
  auto nextSend = std::chrono::steady_clock::now();
  auto nextReport = nextSend + 5s;
  std::uint64_t heard = 0, heardPoses = 0, heardShots = 0, heardVehicles = 0;
  // --pause-at: when someone else was first there, and how far the pause got.
  std::optional<std::chrono::steady_clock::time_point> togetherSince;
  enum class PauseStep { Waiting, Paused, Done } pauseStep = PauseStep::Waiting;
  // Loot: our drop (--drop-item), the others' world items to pick up
  // (--take-items: when), and dead NPCs' bodies to take away (--remove-corpses).
  bool dropped = false;
  std::optional<std::chrono::steady_clock::time_point> dropAt;
  std::map<tandem::proto::WorldItemId,
           std::pair<std::chrono::steady_clock::time_point, tandem::proto::WorldItem>>
      toTake;
  std::map<tandem::proto::NpcId, std::chrono::steady_clock::time_point> corpses;
  std::optional<PlayerState> otherV; // the last state heard of another player

  while (!g_stop) {
    for (const auto &event : endpoint->Update(5ms)) {
      std::visit(
          [&](const auto &e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, tandem::events::Joined>)
              Log("in the session: player {}, host {}", e.you, e.host);
            else if constexpr (std::is_same_v<E, tandem::events::PlayerJoined>)
              Log("+ player {} ({})", e.player.id, e.player.name);
            else if constexpr (std::is_same_v<E, tandem::events::PlayerLeft>)
              Log("- player {}", e.player);
            else if constexpr (std::is_same_v<E, tandem::events::PlayerReturned>)
              Log("= player {} ({}) is back", e.player.id, e.player.name);
            else if constexpr (std::is_same_v<E, tandem::events::RemoteState>) {
              ++heard;
              puppeteer.Heard(e.state);
              otherV = e.state;
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteAppearance>) {
              Log("look from player {}: {} keys, {} items", e.appearance.player,
                  e.appearance.look.size(), e.appearance.items.size());
              if (options.mode == Mode::Mirror || options.mode == Mode::Front) {
                auto echo = e.appearance;
                echo.body = options.body;
                endpoint->SetLocalAppearance(std::move(echo));
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteHeld>) {
              Log("player {} holds {:#x} / {:#x}", e.held.player, e.held.right, e.held.left);
              if (options.mode == Mode::Mirror || options.mode == Mode::Front)
                endpoint->SetLocalHeld(e.held);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcSpawn>) {
              liveNpcs.insert(e.spawn.npc);
              if (!options.recordFile.empty())
                recording.Add(e.spawn);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcStates>) {
              if (!options.recordFile.empty())
                recording.Add(e.states);
              // Who each NPC has detected, when it changes.
              for (const auto &entry : e.states.npcs)
                if (auto &seen = detection[entry.npc]; seen != entry.detected) {
                  Log("npc {} has detected players {:#x}", entry.npc, entry.detected);
                  seen = entry.detected;
                }
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcGone>) {
              liveNpcs.erase(e.gone.npc);
              if (e.gone.reason == 1)
                Log("npc {} died", e.gone.npc);
              if (!options.recordFile.empty())
                recording.Add(e.gone);
            } else if constexpr (std::is_same_v<E, tandem::events::NpcEffectBy>) {
              for (const auto effect : e.effect.effects)
                Log("npc {} gets effect {:#x} from player {}", e.effect.npc, effect,
                    e.effect.player);
            } else if constexpr (std::is_same_v<E, tandem::events::NpcHitBy>) {
              // As the host of a replay: the NPC takes it (100 health each).
              auto &health = npcHealth.try_emplace(e.hit.npc, 1.f).first->second;
              health -= (e.hit.flags & tandem::proto::kHitLethal) ? health : e.hit.damage / 100.f;
              Log("npc {} hit by player {} for {:.0f} (flags {}), health {:.0f}%", e.hit.npc,
                  e.hit.player, e.hit.damage, e.hit.flags, std::max(health, 0.f) * 100.f);
              if (health <= 0.f) {
                deadNpcs.insert(e.hit.npc);
                if (options.removeCorpsesSeconds >= 0)
                  corpses[e.hit.npc] = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(options.removeCorpsesSeconds);
                endpoint->SendNpcGone({.npc = e.hit.npc, .reason = 1, .killer = e.hit.player});
                Log("npc {} died", e.hit.npc);
              } else
                endpoint->SendNpcHealth({.npc = e.hit.npc, .health = health});
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcHealth>) {
              if (options.hitNpcsSeconds)
                Log("npc {} health {:.0f}%", e.health.npc, e.health.health * 100.f);
              if (!options.recordFile.empty())
                recording.Add(e.health);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteSeat>) {
              Log("player {} sits in {:#x} (seat {:#x}, car of player {})", e.seat.player,
                  e.seat.vehicle, e.seat.seat, e.seat.owner);
              // A passenger in the car we drive: our radio again, for them.
              if (options.mode == Mode::Drive && options.radioStation && seated && !parked &&
                  e.seat.vehicle != 0 && e.seat.owner == endpoint->You())
                sendRadio(std::format("player {} got in", e.seat.player));
              // Echo: the same seat in our own copy of the car.
              if (options.mode == Mode::Mirror || options.mode == Mode::Front) {
                auto echo = e.seat;
                echo.owner = tandem::proto::kNoPlayer;
                endpoint->SetLocalSeat(echo);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicle>) {
              ++heardVehicles;
              if (options.mode == Mode::Mirror || options.mode == Mode::Front) {
                auto echo = e.vehicle;
                echo.position.x += options.offset.x;
                echo.position.y += options.offset.y;
                echo.position.z += options.offset.z;
                endpoint->SendLocalVehicle(echo);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicleRadio>) {
              Log("player {}'s car radio: {} (track {:#x})", e.radio.player,
                  StationName(e.radio.on ? e.radio.station : -1), e.radio.track);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteShot>) {
              ++heardShots;
              Log("player {}: weapon event {}", e.shot.player, e.shot.effect);
              if (options.mode == Mode::Mirror || options.mode == Mode::Front)
                endpoint->SendLocalShot(e.shot);
            } else if constexpr (std::is_same_v<E, tandem::events::RemotePose>) {
              ++heardPoses;
              puppeteer.HeardPose(e.pose);
            } else if constexpr (std::is_same_v<E, tandem::events::PauseChanged>) {
              Log("player {} {} (pause menu)", e.pause.player,
                  e.pause.paused ? "paused" : "resumed");
            } else if constexpr (std::is_same_v<E, tandem::events::WorldItemAppeared>) {
              const auto &item = e.item;
              Log("world item {} from player {}: {} stack(s), first {:#x} x{} (token {})", item.id,
                  item.dropper, item.stacks.size(), item.stacks.empty() ? 0 : item.stacks[0].record,
                  item.stacks.empty() ? 0 : item.stacks[0].quantity, item.token);
              if (options.takeItemsSeconds >= 0 && item.dropper != endpoint->You())
                toTake[item.id] = {std::chrono::steady_clock::now() +
                                       std::chrono::seconds(options.takeItemsSeconds),
                                   item};
            } else if constexpr (std::is_same_v<E, tandem::events::WorldItemTakenBy>) {
              Log("world item {} stack {} taken by player {}", e.taken.id, e.taken.stack,
                  e.taken.taker);
              // Someone was first: nothing left of it for us to claim.
              if (auto it = toTake.find(e.taken.id); it != toTake.end()) {
                auto &stacks = it->second.second.stacks;
                if (e.taken.stack < stacks.size())
                  stacks[e.taken.stack].quantity = 0;
              }
            } else if constexpr (std::is_same_v<E, tandem::events::Ended>) {
              Log("session ended: {}", e.reason);
              g_stop = 1;
            }
          },
          event);
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= nextSend) {
      nextSend += interval;
      if (nextSend < now)
        nextSend = now + interval;
      if (auto state = puppeteer.Next(tandem::net::NowUs()))
        endpoint->SendLocalState(*state);
      if (auto pose = puppeteer.NextPose(tandem::net::NowUs(), poseClip))
        endpoint->SendLocalPose(*pose);
      if (options.mode == Mode::Drive && !parked) {
        const auto nowUs = tandem::net::NowUs();
        if (!seated && endpoint->IsJoined()) {
          endpoint->SetLocalSeat(
              {.vehicle = TweakDBID(options.vehicle), .seat = CName("seat_front_left")});
          Log("driving {} ({:#x})", options.vehicle, TweakDBID(options.vehicle));
          seated = true;
          seatedAtUs = nowUs;
          if (options.radioStation)
            sendRadio("at the wheel");
        }
        if (auto car = puppeteer.Drive(nowUs).second) {
          endpoint->SendLocalVehicle(*car);
          lastCar = car;
        }
        if (seated && options.driveSeconds > 0 &&
            nowUs - seatedAtUs > static_cast<std::uint64_t>(options.driveSeconds) * 1'000'000) {
          endpoint->SetLocalSeat({}); // on foot, beside where the car stopped
          parked = true;
          if (lastCar)
            puppeteer.Park(lastCar->position);
          Log("out of the car");
        }
      }
    }
    if (options.host && (options.timeOfDayMinutes || !options.weather.empty()) &&
        now >= nextWorld) {
      nextWorld = now + 5s;
      // Day 1 at the given time, running about 8 game seconds a second.
      const auto elapsed = static_cast<int>((tandem::net::NowUs() - startUs) / 1'000'000);
      tandem::proto::World world;
      world.gameTimeSeconds = 86400 + options.timeOfDayMinutes.value_or(12 * 60) * 60 + elapsed * 8;
      world.weather = options.weather.empty() ? 0 : CName(options.weather);
      endpoint->SetWorld(world);
    }
    // --pause-at: open the pause menu, then close it.
    if (options.pauseAtSeconds >= 0 && pauseStep != PauseStep::Done) {
      if (!togetherSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
        togetherSince = now;
      const auto pauseAt = togetherSince
                               ? *togetherSince + std::chrono::seconds(options.pauseAtSeconds)
                               : std::chrono::steady_clock::time_point::max();
      if (pauseStep == PauseStep::Waiting && now >= pauseAt) {
        endpoint->SetLocalPaused(true);
        pauseStep = PauseStep::Paused;
        Log("pause menu open: pausing everyone for {} s", options.pauseForSeconds);
      } else if (pauseStep == PauseStep::Paused &&
                 now >= pauseAt + std::chrono::seconds(options.pauseForSeconds)) {
        endpoint->SetLocalPaused(false);
        pauseStep = PauseStep::Done;
        Log("pause menu closed");
      }
    }
    // --drop-item: one world item 1.5 m in front of the other V, once.
    if (!options.dropItem.empty() && !dropped && endpoint->IsJoined() && otherV) {
      if (!dropAt)
        dropAt = now + 5s;
      if (now >= *dropAt) {
        dropped = true;
        const float yaw = tandem::sync::YawOf(otherV->orientation);
        const Vec3 at{otherV->position.x - std::sin(yaw) * 1.5f,
                      otherV->position.y + std::cos(yaw) * 1.5f, otherV->position.z};
        endpoint->DropWorldItem(
            {.token = 1,
             .position = at,
             .stacks = {{.record = TweakDBID(options.dropItem), .quantity = 1}}});
        Log("dropped world item {} x1 at ({:.1f}, {:.1f}, {:.1f})", options.dropItem, at.x, at.y,
            at.z);
      }
    }
    // --take-items: every stack nobody took yet, once its time comes.
    for (auto it = toTake.begin(); it != toTake.end();) {
      if (now < it->second.first) {
        ++it;
        continue;
      }
      const auto &item = it->second.second;
      for (std::size_t i = 0; i < item.stacks.size(); ++i)
        if (item.stacks[i].quantity > 0) {
          endpoint->TakeWorldItem(item.id, static_cast<std::uint8_t>(i));
          Log("claimed world item {} stack {}", item.id, i);
        }
      it = toTake.erase(it);
    }
    // --remove-corpses: the bodies of NPCs a guest killed go.
    for (auto it = corpses.begin(); it != corpses.end();) {
      if (now < it->second) {
        ++it;
        continue;
      }
      endpoint->SendNpcGone({.npc = it->first, .reason = 0});
      Log("npc {}'s body taken away", it->first);
      it = corpses.erase(it);
    }
    // --hit-npcs: one NPC at a time until it dies.
    if (options.hitNpcsSeconds && !options.host && endpoint->IsJoined() && !liveNpcs.empty() &&
        now >= nextHit) {
      nextHit = now + std::chrono::seconds(options.hitNpcsSeconds);
      if (!hitTarget || !liveNpcs.contains(*hitTarget))
        hitTarget = *liveNpcs.begin();
      endpoint->SendNpcHit({.npc = *hitTarget, .damage = 35.f});
      Log("hit npc {} for 35", *hitTarget);
      // The first hit on each NPC also sets it on fire (a status effect our
      // hit applied, for the host to put on the real NPC).
      if (burnt.insert(*hitTarget).second) {
        endpoint->SendNpcEffect(
            {.npc = *hitTarget, .effects = {TweakDBID("BaseStatusEffect.Burning")}});
        Log("set npc {} on fire", *hitTarget);
      }
    }
    // Replay, once someone is there to see it, at the recording's pace.
    if (options.host && replayNext < replay.size() && endpoint->Players().size() > 1) {
      const auto nowUs = tandem::net::NowUs();
      if (!replayStartUs)
        replayStartUs = nowUs;
      while (replayNext < replay.size() && replay[replayNext].atUs <= nowUs - *replayStartUs) {
        auto message = replay[replayNext++].message;
        // --offset moves the replayed NPCs, to tell them from the game's own.
        const auto shift = [&](Vec3 &p) {
          p.x += options.offset.x;
          p.y += options.offset.y;
          p.z += options.offset.z;
        };
        // An NPC a guest killed is out of the rest of the recording.
        if (auto *spawn = std::get_if<tandem::proto::NpcSpawn>(&message)) {
          shift(spawn->position);
          endpoint->SendNpcSpawn(*spawn);
        } else if (auto *states = std::get_if<tandem::proto::NpcStates>(&message)) {
          std::erase_if(states->npcs,
                        [&](const auto &entry) { return deadNpcs.contains(entry.npc); });
          for (auto &entry : states->npcs)
            shift(entry.position);
          endpoint->SendNpcStates(*states);
        } else if (const auto *gone = std::get_if<tandem::proto::NpcGone>(&message)) {
          if (!deadNpcs.contains(gone->npc))
            endpoint->SendNpcGone(*gone);
        } else if (const auto *health = std::get_if<tandem::proto::NpcHealth>(&message))
          endpoint->SendNpcHealth(*health);
      }
      if (replayNext == replay.size())
        Log("replay: done");
    }
    if (now >= nextReport) {
      nextReport = now + 5s;
      if (!options.recordFile.empty())
        Log("recorded {} NPC messages", recording.Count());
      const auto stats = endpoint->HostLinkStats();
      Log("{} player(s), {} states, {} poses, {} shots, {} car states heard{}",
          endpoint->Players().size(), heard, heardPoses, heardShots, heardVehicles,
          stats ? std::format(", rtt {} ms, loss {:.1f}%", stats->rttMs, stats->packetLoss * 100)
                : "");
    }
  }

  endpoint->Leave("tandem-peer stopped");
  return 0;
}
