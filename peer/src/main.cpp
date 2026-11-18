// tandem-peer: a stand-in second player on Linux. It speaks the real protocol
// through the same core as the game plugin, so one copy of the game can be
// tested against it.
//
//   tandem-peer host [options]          the game joins it
//   tandem-peer join ADDRESS[:PORT]     it joins the game
//   tandem-peer join --code CODE --relay RELAY   it joins by a join code
//
// Relay (--relay HOST[:PORT]): a host is also reachable through tandem-relay
// and logs its join code; a guest joins by --code through it, after trying
// ADDRESS directly when one is given.
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
// Fights: --hit-npcs (join) hits the host's NPCs; --hit-you (host) has one of
// its NPCs hit every guest's V; a guest logs the NPC hits it receives.
// Stealth: as host, --npc puts an NPC in front of the guest and --detect-at
// makes the NPCs it sends notice, detect and lose the guests (NpcAwareness,
// NpcEntry::detected); --camo turns "its optical camo" on, --shoot (join)
// fires gunshots its host's NPCs hear. Stealth and noises heard are logged.
// Quickhacks: --hack-npcs (as a guest) uploads on the host's NPCs and lands
// the effect; --hack-player (as the host of a replay) has an NPC hack the
// guest's V; --hack-car brakes the car another player drives.
// Sandevistan: --sandevistan-at switches "its Sandevistan" on that many
// seconds into the session for --sandevistan-for seconds, at
// --sandevistan-scale: the host slows the NPCs around its V (time bubble);
// the time abilities it hears are logged.
// Body pose: mirror and front echo your V's pose; --pose FILE (a .tpose made by
// tools/re/pose_export.py from a recording) plays a recorded pose instead, in
// a loop, with any mode.
// Story (milestone 6): --story FILE plays a scripted host story to the game
// as a guest (journal, tracked quest, calls, lines, hubs, tier, rewards; the
// format is in tools/scenarios/story-host.story); as a guest, --answer,
// --reply and --suggest act on the host's calls, texts and hubs, and every
// story message heard is logged (and recorded with --record).

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
#include <random>
#include <set>
#include <sstream>
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
  // Our own look: random clothes (so tests see a V of its own, not a copy
  // of the other player), or an echo of theirs.
  bool echoLook{};
  std::optional<unsigned> lookSeed;
  int driveSeconds{0}; // drive: get out after this long (0: never)
  // drive: the car's radio (ERadioStationList index, -1 off); unset: none sent
  std::optional<int> radioStation;
  int hitNpcsSeconds{0}; // join: hit one of the host's NPCs this often (0: never)
  int hitYouSeconds{0};  // host: one of its NPCs hits each guest's V this often (0: never)
  // host: the world the game follows (time of day, weather name); unset: none sent
  std::optional<int> timeOfDayMinutes;
  std::string weather;
  std::string recordFile;   // join: save the host's NPC messages here
  std::string replayFile;   // host: send a recording's NPC messages again
  int pauseAtSeconds{-1};   // open the pause menu this long into the session (-1: never)
  int pauseForSeconds{3};   // ...for this long
  std::string npcRecord;    // host: one NPC of this record in front of the guest
  int detectAtSeconds{-1};  // host: its NPCs detect the guests this long in (-1: never)
  int detectForSeconds{10}; // ...for this long
  int camoAtSeconds{-1};    // optical camo on this long into the session, for 5 s
  int shootSeconds{0};      // join: a gunshot (shot + noise) this often (0: never)
  // Quickhacks: join: upload --hack-action on a host NPC every N s, then land
  // --hack-effect; host: one of our NPCs hacks the guest (--npc-hack) every
  // N s; either: hack the car another player drives every N s.
  int hackNpcsSeconds{0};
  std::string hackAction{"QuickHack.BlindHack"};
  std::string hackEffect{"BaseStatusEffect.Blind"};
  int hackPlayerSeconds{0};
  std::string npcHack{"AIQuickHack.HackRevealPosition"};
  int hackCarSeconds{0};
  std::string storyFile;         // host: a scripted story (tools/scenarios/story-host.story)
  int answerSeconds{-1};         // join: answer the host's ringing call after this long (-1: never)
  std::uint32_t replyEntry{};    // join: reply to the text with this choice entry hash
  int suggestIndex{-1};          // join: suggest this option of every hub (-1: never)
  bool check{};                  // parse the options, then exit
  int sandevistanAtSeconds{-1};  // switch a Sandevistan on this long into the session (-1: never)
  int sandevistanForSeconds{5};  // ...for this long
  float sandevistanScale{0.25f}; // ...asking for this world speed
  // tandem-relay: its address, a host token (host), the session's code (join)
  std::optional<tandem::net::RelayOptions> relay;
  std::string code;
  // Faults on what we send (docs/ARCHITECTURE.md, Consistency).
  float drop{}, dup{};
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
  std::cerr
      << "usage: tandem-peer host [options]\n"
         "       tandem-peer join ADDRESS[:PORT] [options]\n"
         "       tandem-peer join --code CODE --relay HOST[:PORT] [options]\n\n"
         "  --port N           UDP port (default 27450)\n"
         "  --relay HOST[:PORT] tandem-relay (default port 27460): host, also reachable\n"
         "                     through it (its join code is logged); join, used when\n"
         "                     ADDRESS does not answer, or with --code only\n"
         "  --relay-token HEX  host: one of the relay's host tokens (tandem-relay token)\n"
         "  --code CODE        join: the host's join code (needs --relay)\n"
         "  --mode M           mirror | ghost | front | drive | idle (default mirror)\n"
         "  --name NAME        player name (default tandem-peer)\n"
         "  --body B           female | male: the puppet rig others see (default female)\n"
         "  --look L           random | echo: random clothes of its own (default), or a\n"
         "                     copy of the other player's look (mirror, front)\n"
         "  --seed N           --look random: the same outfit every time for N\n"
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
         "  --hit-you N        host: an NPC (a replayed one if any) hits each guest's V for\n"
         "                     10 physical every N seconds\n"
         "  --time HH:MM       host: the time of day the game must follow\n"
         "  --weather NAME     host: the weather it must follow (24h_weather_rain...)\n"
         "  --record FILE      join: save the host's NPCs (spawns, states, gone) to FILE\n"
         "  --replay FILE      host: send a recording's NPCs again, at its pace (moved by\n"
         "                     --offset)\n"
         "  --pause-at N       open the pause menu N seconds after another player is in the\n"
         "                     session: every game pauses\n"
         "  --pause-for N      --pause-at: close it again after N seconds (default 3)\n"
         "  --npc RECORD       host: one NPC of RECORD standing 6 m in front of the guest\n"
         "  --detect-at N      host: N seconds after a guest is there, the NPCs it sends\n"
         "                     notice it (meter rising over 2 s), then have detected it\n"
         "  --detect-for N     --detect-at: and lose it again after N seconds (default 10)\n"
         "  --camo N           turn optical camo on N seconds after another player is in the\n"
         "                     session, for 5 seconds\n"
         "  --shoot N          join: fire a gunshot (shot and noise) every N seconds\n"
         "  --hack-npcs N      join: upload a quickhack on one of the host's NPCs every N\n"
         "                     seconds (1.5 s upload), then land its effect\n"
         "  --hack-action REC  --hack-npcs: the quickhack (default QuickHack.BlindHack)\n"
         "  --hack-effect REC  --hack-npcs: the effect it lands (default BaseStatusEffect.Blind)\n"
         "  --hack-player N    host: one of our (replayed) NPCs hacks the guest's V every N\n"
         "                     seconds\n"
         "  --npc-hack REC     --hack-player: the NPC's hack (default\n"
         "                     AIQuickHack.HackRevealPosition, a trace)\n"
         "  --hack-car N       hack the car another player drives (brakes, 3 s) every N\n"
         "                     seconds\n"
         "  --story FILE       host: play a scripted story (journal, calls, lines, hubs,\n"
         "                     rewards; format in tools/scenarios/story-host.story)\n"
         "  --answer N         join: answer the host's ringing call after N seconds\n"
         "  --reply HASH       join: reply to a text with this choice entry (journal hash)\n"
         "  --suggest N        join: suggest option N of every choice hub the host shows\n"
         "  --sandevistan-at N switch a Sandevistan on N seconds after another player is in\n"
         "                     the session: the host slows the NPCs around this V\n"
         "  --sandevistan-for N  --sandevistan-at: switch it off after N seconds (default 5)\n"
         "  --sandevistan-scale S  --sandevistan-at: the world speed it asks for (default 0.25)\n"
         "  --drop P           never send a message with probability P (0..1), reliable ones\n"
         "                     too, as a bug would lose them: digests and resyncs must heal it\n"
         "  --dup P            send a state message twice with probability P (0..1): it must\n"
         "                     be applied once\n";
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
      Usage("join needs an address or --code");
    if (args[1].starts_with("--")) {
      options.address.clear(); // by --code, through the relay only
    } else {
      auto target = args[1];
      if (auto colon = target.rfind(':'); colon != std::string_view::npos) {
        options.port = Number<std::uint16_t>(target.substr(colon + 1));
        target = target.substr(0, colon);
      }
      options.address = std::string(target);
      i = 2;
    }
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
    else if (flag == "--drop" || flag == "--dup") {
      const auto p = Number<float>(value);
      if (!(p >= 0.f && p <= 1.f))
        Usage(std::format("{} is a probability (0..1), not {}", flag, value));
      (flag == "--drop" ? options.drop : options.dup) = p;
    } else if (flag == "--hack-npcs")
      options.hackNpcsSeconds = Number<int>(value);
    else if (flag == "--hack-action")
      options.hackAction = std::string(value);
    else if (flag == "--hack-effect")
      options.hackEffect = std::string(value);
    else if (flag == "--hack-player")
      options.hackPlayerSeconds = Number<int>(value);
    else if (flag == "--npc-hack")
      options.npcHack = std::string(value);
    else if (flag == "--hack-car")
      options.hackCarSeconds = Number<int>(value);
    else if (flag == "--sandevistan-at")
      options.sandevistanAtSeconds = Number<int>(value);
    else if (flag == "--sandevistan-for")
      options.sandevistanForSeconds = Number<int>(value);
    else if (flag == "--sandevistan-scale")
      options.sandevistanScale = Number<float>(value);
    else if (flag == "--hit-npcs")
      options.hitNpcsSeconds = Number<int>(value);
    else if (flag == "--hit-you")
      options.hitYouSeconds = Number<int>(value);
    else if (flag == "--npc")
      options.npcRecord = std::string(value);
    else if (flag == "--detect-at")
      options.detectAtSeconds = Number<int>(value);
    else if (flag == "--detect-for")
      options.detectForSeconds = Number<int>(value);
    else if (flag == "--camo")
      options.camoAtSeconds = Number<int>(value);
    else if (flag == "--shoot")
      options.shootSeconds = std::max(1, Number<int>(value));
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
    } else if (flag == "--look") {
      if (value == "echo")
        options.echoLook = true;
      else if (value == "random")
        options.echoLook = false;
      else
        Usage(std::format("unknown look {}", value));
    } else if (flag == "--seed")
      options.lookSeed = Number<unsigned>(value);
    else if (flag == "--vehicle")
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
    } else if (flag == "--relay") {
      if (!options.relay)
        options.relay.emplace();
      if (!tandem::net::ParseRelayAddress(value, options.relay->address, options.relay->port))
        Usage(std::format("--relay is HOST[:PORT], not {}", value));
    } else if (flag == "--relay-token") {
      const auto token = tandem::net::ParseRelayToken(value);
      if (!token)
        Usage("--relay-token is 64 hex digits (tandem-relay token)");
      if (!options.relay)
        options.relay.emplace();
      options.relay->token = *token;
    } else if (flag == "--code") {
      const auto code = tandem::net::NormalizeJoinCode(value);
      if (!code)
        Usage(std::format("not a join code: {}", value));
      options.code = *code;
    } else if (flag == "--story") {
      options.storyFile = value;
    } else if (flag == "--answer") {
      options.answerSeconds = Number<int>(value);
    } else if (flag == "--reply") {
      options.replyEntry = Number<std::uint32_t>(value);
    } else if (flag == "--suggest") {
      options.suggestIndex = Number<int>(value);
    } else
      Usage(std::format("unknown option {}", flag));
  }
  if (!options.host && !options.check) {
    if (options.address.empty() && options.code.empty())
      Usage("join needs an address or --code");
    if (!options.code.empty() && (!options.relay || options.relay->address.empty()))
      Usage("--code needs --relay");
  }
  if (options.relay && options.relay->address.empty() && !options.check)
    Usage("--relay-token needs --relay");
  if (options.relay)
    options.relay->code = options.code;
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

// A scripted host story (--story FILE): one step per line,
//   SECONDS VERB ARGS...    seconds after a guest is in the session
// Verbs (hashes are journal path hashes as the game logs them, `cmd story
// dump`; names are CNames or TweakDB records by name):
//   snapshot HASH:STATE ...      the journal as a joiner gets it
//   journal HASH STATE [notify]  one change (notify: the toast and sound)
//   tracked HASH                 the tracked entry
//   call CONTACT PHASE           the host's call: 1 ringing, 2 started, 3 ended
//   line TYPE SPEAKER TEXT...    a dialogue line (type 1 regular, 2 holocall;
//                                SPEAKER: host for our V, or a name)
//   hide                         the last line goes
//   hub ID TITLE|OPTION|...      a choice hub (no options: closed)
//   pick ID INDEX                the host picks (every hub closes)
//   tier N                       the host's scene tier (1..5)
//   reward RECORD [QUESTHASH]    a quest reward for every guest
//   effect RECORD on|off         a story status effect on the host's V
//                                (a relic glitch: BaseStatusEffect.JohnnySicknessLow)
//   replacer on|off              the host plays as Johnny (StoryState flag)
// Lines starting with # are comments.
struct StoryStep {
  double at{};
  std::string verb;
  std::vector<std::string> args;
  std::string rest; // everything after the verb
};

std::vector<StoryStep> LoadStory(const std::string &path, std::string &error) {
  std::vector<StoryStep> steps;
  std::ifstream in(path);
  if (!in) {
    error = std::format("cannot read {}", path);
    return steps;
  }
  std::string line;
  for (int number = 1; std::getline(in, line); ++number) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream words(line);
    StoryStep step;
    if (!(words >> step.at >> step.verb)) {
      error = std::format("{}:{}: expected SECONDS VERB", path, number);
      return {};
    }
    std::getline(words, step.rest);
    if (const auto first = step.rest.find_first_not_of(' '); first != std::string::npos)
      step.rest = step.rest.substr(first);
    std::istringstream rest(step.rest);
    for (std::string word; rest >> word;)
      step.args.push_back(word);
    steps.push_back(std::move(step));
  }
  return steps;
}

std::uint32_t Hash32(const std::string &text) {
  return static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
}

// One story step as the host's game would send it.
std::optional<tandem::proto::Message> StoryMessage(const StoryStep &step, std::uint64_t &lastLine,
                                                   tandem::proto::PlayerId host) {
  namespace proto = tandem::proto;
  const auto &a = step.args;
  try {
    if (step.verb == "snapshot") {
      proto::Journal journal{.snapshot = true};
      for (const auto &pair : a) {
        const auto colon = pair.find(':');
        journal.entries.push_back({Hash32(pair.substr(0, colon)),
                                   static_cast<std::uint8_t>(std::stoi(pair.substr(colon + 1))),
                                   0});
      }
      return journal;
    }
    if (step.verb == "journal" && a.size() >= 2)
      return proto::Journal{.entries = {{Hash32(a[0]), static_cast<std::uint8_t>(std::stoi(a[1])),
                                         static_cast<std::uint8_t>(a.size() > 2)}}};
    if (step.verb == "tracked" && !a.empty())
      return proto::Tracked{Hash32(a[0])};
    if (step.verb == "call" && a.size() >= 2)
      return proto::PhoneCall{.contact = CName(a[0]),
                              .phase = static_cast<std::uint8_t>(std::stoi(a[1])),
                              .mode = 1,
                              .flags = proto::kCallRejectable | proto::kCallShowAvatar};
    if (step.verb == "line" && a.size() >= 3) {
      proto::Subtitle line{
          .line = ++lastLine, .type = static_cast<std::uint8_t>(std::stoi(a[0])), .duration = 4.f};
      if (a[1] == "host")
        line.speakerPlayer = host;
      else
        line.speakerName = a[1];
      const auto text = step.rest.find(a[1]);
      line.text = step.rest.substr(text + a[1].size() + 1);
      return line;
    }
    if (step.verb == "hide")
      return proto::Subtitle{.line = lastLine, .flags = proto::kLineHide};
    if (step.verb == "hub" && !a.empty()) {
      proto::ChoiceHub hub{.hub = std::stoi(a[0])};
      const auto rest = step.rest.substr(step.rest.find(a[0]) + a[0].size());
      if (const auto bar = rest.find('|'); bar != std::string::npos) {
        hub.title = rest.substr(rest.find_first_not_of(' '), bar - rest.find_first_not_of(' '));
        std::size_t start = bar + 1;
        for (;;) {
          const auto next = rest.find('|', start);
          hub.options.push_back({rest.substr(start, next - start), 0});
          if (next == std::string::npos)
            break;
          start = next + 1;
        }
      }
      return hub;
    }
    if (step.verb == "pick" && a.size() >= 2)
      return proto::ChoiceCursor{.hub = std::stoi(a[0]),
                                 .index = static_cast<std::int8_t>(std::stoi(a[1])),
                                 .kind = proto::kCursorPicked};
    if (step.verb == "tier" && !a.empty())
      return proto::StoryState{.tier = static_cast<std::uint8_t>(std::stoi(a[0]))};
    if (step.verb == "replacer" && !a.empty())
      return proto::StoryState{.tier = 1,
                               .flags = a[0] == "on" ? proto::kStoryReplacer : std::uint8_t{0}};
    if (step.verb == "effect" && a.size() >= 2)
      return proto::StoryEffect{.effect = TweakDBID(a[0]), .applied = a[1] == "on"};
    if (step.verb == "reward" && !a.empty())
      return proto::QuestReward{.reward = TweakDBID(a[0]),
                                .quest = a.size() > 1 ? Hash32(a[1]) : 0};
  } catch (const std::exception &) {
  }
  return std::nullopt;
}

// A story message heard, in one line (for the scenarios' expect_peer).
std::string Describe(const tandem::proto::Message &message) {
  namespace proto = tandem::proto;
  if (const auto *journal = std::get_if<proto::Journal>(&message))
    return std::format("journal: {} {} entries", journal->snapshot ? "snapshot of" : "change of",
                       journal->entries.size());
  if (const auto *tracked = std::get_if<proto::Tracked>(&message))
    return std::format("tracked: {}", tracked->hash);
  if (const auto *call = std::get_if<proto::PhoneCall>(&message))
    return std::format("call: contact {:#x} phase {} answered by {}{}", call->contact, call->phase,
                       call->answeredBy, call->flags & proto::kCallRejected ? " (rejected)" : "");
  if (const auto *action = std::get_if<proto::PhoneAction>(&message))
    return std::format("phone: action {} on {:#x} by player {}", action->action, action->contact,
                       action->player);
  if (const auto *reply = std::get_if<proto::PhoneReply>(&message))
    return std::format("phone: reply {} by player {}", reply->choiceEntry, reply->player);
  if (const auto *line = std::get_if<proto::Subtitle>(&message))
    return (line->flags & proto::kLineHide)
               ? std::format("line: {} hidden", line->line)
               : std::format("line: type {} from {}: {}", line->type,
                             line->speakerPlayer ? std::format("player {}", line->speakerPlayer)
                                                 : line->speakerName,
                             line->text);
  if (const auto *hub = std::get_if<proto::ChoiceHub>(&message))
    return hub->options.empty() ? std::format("hub: {} closed", hub->hub)
                                : std::format("hub: {} '{}' with {} options", hub->hub, hub->title,
                                              hub->options.size());
  if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&message))
    return std::format("cursor: player {} hub {} index {} kind {}", cursor->player, cursor->hub,
                       cursor->index, cursor->kind);
  if (const auto *state = std::get_if<proto::StoryState>(&message))
    return std::format("story state: player {} tier {} flags {}", state->player, state->tier,
                       state->flags);
  if (const auto *reward = std::get_if<proto::QuestReward>(&message))
    return std::format("reward: {:#x} (quest {})", reward->reward, reward->quest);
  if (const auto *effect = std::get_if<proto::StoryEffect>(&message))
    return std::format("story effect: {:#x} {} on player {}", effect->effect,
                       effect->applied ? "on" : "off", effect->player);
  return "story: ?";
}

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

// A look of our own: no face keys (the puppet keeps its record's face and
// body) and one random piece per slot from clothing records the game's own
// scripts name (their crafting specs end in _Crafting; the item is the same
// name without it).
tandem::proto::Appearance RandomLook(tandem::proto::Body body, unsigned seed) {
  static const std::vector<std::vector<std::string_view>> slots = {
      {"Items.FormalShirt_01_rich_06", "Items.FormalShirt_02_basic_03",
       "Items.LooseShirt_01_rich_02", "Items.Fixer_01_Set_TShirt"},
      {"Items.Jacket_19_basic_04", "Items.Jacket_20_basic_01", "Items.Jacket_01_rich_01",
       "Items.Jacket_17_rich_06", "Items.Coat_01_basic_03", "Items.Coat_04_rich_02",
       "Items.FormalJacket_02_basic_02", "Items.FormalJacket_04_basic_02"},
      {"Items.Pants_02_basic_01", "Items.Pants_04_basic_01", "Items.Pants_08_rich_01",
       "Items.FormalPants_02_rich_01", "Items.FormalPants_03_rich_02",
       "Items.FormalSkirt_01_rich_01", "Items.FormalSkirt_02_rich_03"},
      {"Items.Boots_03_basic_02", "Items.Boots_07_basic_01", "Items.Boots_09_rich_01",
       "Items.CasualShoes_01_basic_01", "Items.CasualShoes_02_basic_01",
       "Items.CasualShoes_05_basic_03", "Items.FormalShoes_01_rich_02",
       "Items.FormalShoes_03_rich_01"},
      // Head and face: sometimes nothing.
      {"", "", "Items.Cap_02_basic_01", "Items.Cap_03_rich_02", "Items.Hat_02_rich_02",
       "Items.Hat_03_basic_02"},
      {"", "", "Items.Glasses_01_basic_03", "Items.Glasses_03_basic_06"},
  };
  std::mt19937 random(seed);
  tandem::proto::Appearance look{.body = body};
  std::string picked;
  for (const auto &slot : slots) {
    const auto name = slot[random() % slot.size()];
    if (name.empty())
      continue;
    look.items.push_back(TweakDBID(name));
    picked += std::format(" {}", name.substr(6));
  }
  Log("our look: random (seed {}):{}", seed, picked);
  return look;
}

int main(int argc, char **argv) {
  const auto options = Parse(argc, argv);
  if (options.check) {
    std::cout << "options ok\n";
    return 0;
  }
  std::signal(SIGINT, [](int) { g_stop = 1; });
  std::signal(SIGTERM, [](int) { g_stop = 1; });

  std::string error;
  // A guest with a code but no address goes through the relay only.
  const tandem::JoinTarget target{.address = options.address,
                                  .port = options.port,
                                  .relay = options.code.empty() ? std::nullopt : options.relay};
  auto endpoint = options.host
                      ? tandem::Endpoint::Host({options.name, options.body}, options.port, error,
                                               {}, options.relay)
                      : tandem::Endpoint::Join({options.name, options.body}, target, error);
  if (!endpoint) {
    std::cerr << "tandem-peer: " << error << '\n';
    return 1;
  }
  if (options.drop > 0.f || options.dup > 0.f) {
    endpoint->SetFaults({.drop = options.drop, .dup = options.dup, .seed = std::random_device{}()});
    Log("faults: {:.0f}% of what we send is lost, {:.0f}% of our state sent twice",
        options.drop * 100, options.dup * 100);
  }
  if (!options.echoLook)
    endpoint->SetLocalAppearance(
        RandomLook(options.body, options.lookSeed.value_or(std::random_device{}())));
  if (options.host)
    Log("hosting on UDP {} as {}{}", options.port, options.name,
        options.relay
            ? std::format(", and on relay {}:{}", options.relay->address, options.relay->port)
            : "");
  else if (options.address.empty())
    Log("joining code {} through relay {}:{} as {}", tandem::net::DisplayJoinCode(options.code),
        options.relay->address, options.relay->port, options.name);
  else
    Log("joining {}:{} as {}{}", options.address, options.port, options.name,
        target.relay ? " (the relay if it does not answer)" : "");
  // The relay as last logged: its state, the host's code, the last error.
  std::optional<tandem::net::RelayStatus> relayShown;

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
  // Stealth: --npc's NPC (where the guest was first heard), the NPCs last
  // sent (host), how far each NPC notices us (join), --camo, --shoot.
  constexpr tandem::proto::NpcId kOwnNpc = 50'000;
  std::optional<PlayerState> firstRemote;
  std::optional<tandem::proto::NpcEntry> ownNpc;
  std::vector<tandem::proto::NpcId> sentNpcs;
  std::map<tandem::proto::NpcId, int> noticedStep;
  int detectPhase = 0; // 0 nothing, 1 noticing, 2 detected, 3 lost
  auto nextNpcState = std::chrono::steady_clock::now();
  auto nextAwareness = std::chrono::steady_clock::now();
  int camoStep = 0; // 0 waiting, 1 on, 2 done
  auto nextShot = std::chrono::steady_clock::now();
  std::optional<std::chrono::steady_clock::time_point> sharedSince;
  // --detect-at: while detected, every guest's bit (player - 1).
  const auto detectedBits = [&]() -> std::uint8_t {
    std::uint8_t bits = 0;
    if (detectPhase == 2)
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You() && id >= 1 && id <= 8)
          bits |= static_cast<std::uint8_t>(1u << (id - 1));
    return bits;
  };
  auto nextHit = std::chrono::steady_clock::now();
  auto nextHitYou = std::chrono::steady_clock::now() + std::chrono::seconds(options.hitYouSeconds);
  std::set<tandem::proto::NpcId> replayedNpcs; // host: the replay's live NPCs
  // Quickhacks: our upload's effect to land, the host's uploads to finish,
  // the NPCs a replay spawned, who drives a car.
  struct Pending {
    std::chrono::steady_clock::time_point at;
    tandem::proto::NpcHackState state;
    std::uint64_t effect{};
  };
  std::vector<Pending> pendingHacks;
  std::map<tandem::proto::PlayerId, bool> driving;
  auto nextHack = std::chrono::steady_clock::now() + 5s;
  auto nextPlayerHack = std::chrono::steady_clock::now() + 5s;
  auto nextCarHack = std::chrono::steady_clock::now() + 5s;
  std::vector<NpcRecording::Entry> replay;
  std::size_t replayNext = 0;
  std::optional<std::uint64_t> replayStartUs;
  if (!options.replayFile.empty()) {
    replay = NpcRecording::Load(options.replayFile);
    Log("replay: {} NPC messages from {}", replay.size(), options.replayFile);
  }

  std::vector<StoryStep> story;
  std::size_t storyNext = 0;
  std::optional<std::chrono::steady_clock::time_point> storyStart;
  std::uint64_t storyLine = 0;
  if (!options.storyFile.empty()) {
    story = LoadStory(options.storyFile, error);
    if (!error.empty()) {
      std::cerr << "tandem-peer: " << error << '\n';
      return 1;
    }
    Log("story: {} steps from {}", story.size(), options.storyFile);
  }
  // join: the call to answer, and when.
  std::optional<std::pair<std::uint64_t, std::chrono::steady_clock::time_point>> toAnswer;
  bool replied = false;
  std::optional<std::chrono::steady_clock::time_point> joinedAt;

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
  int resyncsAsked = 0;
  std::uint64_t heard = 0, heardPoses = 0, heardShots = 0, heardVehicles = 0;
  // --pause-at: when someone else was first there, and how far the pause got.
  std::optional<std::chrono::steady_clock::time_point> togetherSince;
  enum class PauseStep { Waiting, Paused, Done } pauseStep = PauseStep::Waiting;
  PauseStep sandevistanStep = PauseStep::Waiting;

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
              if (!firstRemote)
                firstRemote = e.state;
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteStealth>) {
              Log("player {} stealth: flags {:#x}, visibility {:.2f}, camo {:#x}", e.stealth.player,
                  e.stealth.flags, e.stealth.visibility, e.stealth.camo);
            } else if constexpr (std::is_same_v<E, tandem::events::NoiseBy>) {
              Log("player {} noise: stim {}, radius {:.0f}", e.noise.player, e.noise.stim,
                  e.noise.radius);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcAwareness>) {
              // How far each NPC notices us, in quarters, when it changes.
              std::map<tandem::proto::NpcId, int> now;
              for (const auto &entry : e.awareness.entries)
                if (entry.player == endpoint->You())
                  now[entry.npc] = entry.level * 4 / 255;
              for (const auto &[npc, step] : now)
                if (noticedStep[npc] != step)
                  Log("npc {} notices us: {}%", npc, step * 25);
              for (const auto &[npc, step] : noticedStep)
                if (step && !now.contains(npc))
                  Log("npc {} notices us: 0%", npc);
              noticedStep = std::move(now);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteAppearance>) {
              Log("look from player {}: {} keys, {} items", e.appearance.player,
                  e.appearance.look.size(), e.appearance.items.size());
              if (options.echoLook &&
                  (options.mode == Mode::Mirror || options.mode == Mode::Front)) {
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
                endpoint->SendNpcGone({.npc = e.hit.npc, .reason = 1, .killer = e.hit.player});
                Log("npc {} died", e.hit.npc);
              } else
                endpoint->SendNpcHealth({.npc = e.hit.npc, .health = health});
            } else if constexpr (std::is_same_v<E, tandem::events::HitByNpc>) {
              Log("hit by npc {}: {:.1f} physical, {:.1f} thermal, {:.1f} electric, {:.1f} "
                  "chemical (type {}, flags {}, attack {:#x})",
                  e.hit.npc, e.hit.physical, e.hit.thermal, e.hit.electric, e.hit.chemical,
                  e.hit.attackType, e.hit.flags, e.hit.attack);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcHealth>) {
              if (options.hitNpcsSeconds)
                Log("npc {} health {:.0f}%", e.health.npc, e.health.health * 100.f);
              if (!options.recordFile.empty())
                recording.Add(e.health);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteSeat>) {
              Log("player {} sits in {:#x} (seat {:#x}, car of player {})", e.seat.player,
                  e.seat.vehicle, e.seat.seat, e.seat.owner);
              driving[e.seat.player] = e.seat.vehicle != 0 &&
                                       e.seat.seat == CName("seat_front_left") &&
                                       e.seat.owner == e.seat.player;
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
            } else if constexpr (std::is_same_v<E, tandem::events::NpcHackBy>) {
              // As the host of a replay: the NPC is being hacked for that long.
              Log("player {} uploads {:#x} on npc {} ({} ms)", e.hack.player, e.hack.action,
                  e.hack.npc, e.hack.uploadMs);
              tandem::proto::NpcHackState state{.npc = e.hack.npc,
                                                .player = e.hack.player,
                                                .action = e.hack.action,
                                                .uploadMs = e.hack.uploadMs};
              endpoint->SendNpcHackState(state);
              state.step = tandem::proto::HackStep::Done;
              pendingHacks.push_back(
                  {std::chrono::steady_clock::now() + std::chrono::milliseconds(e.hack.uploadMs),
                   state});
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcHackState>) {
              Log("npc {}: upload {:#x} of player {} step {}", e.state.npc, e.state.action,
                  e.state.player, static_cast<int>(e.state.step));
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcStatus>) {
              Log("npc {} {} effect {:#x} (player {})", e.status.npc,
                  e.status.applied ? "has" : "lost", e.status.effect, e.status.player);
            } else if constexpr (std::is_same_v<E, tandem::events::HostPlayerHack>) {
              Log("npc {} hacks us with {:#x} (flags {})", e.hack.npc, e.hack.action, e.hack.flags);
            } else if constexpr (std::is_same_v<E, tandem::events::VehicleHackOn>) {
              Log("player {} hacked our car: {} ({:.1f} s)", e.hack.player,
                  static_cast<int>(e.hack.hack), e.hack.seconds);
            } else if constexpr (std::is_same_v<E, tandem::events::PauseChanged>) {
              Log("player {} {} (pause menu)", e.pause.player,
                  e.pause.paused ? "paused" : "resumed");
            } else if constexpr (std::is_same_v<E, tandem::events::Story>) {
              Log("{}", Describe(e.message));
              if (!options.recordFile.empty())
                recording.Add(e.message);
              namespace proto = tandem::proto;
              if (const auto *call = std::get_if<proto::PhoneCall>(&e.message);
                  call && options.answerSeconds >= 0 && call->phase == 1 &&
                  call->answeredBy == proto::kNoPlayer)
                toAnswer = {call->contact, std::chrono::steady_clock::now() +
                                               std::chrono::seconds(options.answerSeconds)};
              if (const auto *hub = std::get_if<proto::ChoiceHub>(&e.message);
                  hub && options.suggestIndex >= 0 && !hub->options.empty() && !options.host) {
                endpoint->SendStory(
                    proto::ChoiceCursor{.hub = hub->hub,
                                        .index = static_cast<std::int8_t>(options.suggestIndex),
                                        .kind = proto::kCursorSuggest});
                Log("suggest option {} of hub {}", options.suggestIndex, hub->hub);
              }
              // As a scripted host: a guest's answer that counted.
              if (const auto *action = std::get_if<proto::PhoneAction>(&e.message);
                  action && options.host)
                Log("phone: {} by player {}",
                    action->action == proto::kPhoneReject ? "rejected" : "answered",
                    action->player);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteTimeEffect>) {
              Log("player {}: time abilities {:#x} (world speed {:.2f})", e.effect.player,
                  e.effect.abilities, e.effect.scale);
            } else if constexpr (std::is_same_v<E, tandem::events::Resync>) {
              if (e.player == endpoint->You())
                Log("resync: asked the host for domains {:#x}", e.domains);
              else {
                ++resyncsAsked;
                Log("resync: player {} asked for domains {:#x}", e.player, e.domains);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::EpochChanged>) {
              Log("epoch: player {}'s world was rebuilt (reason {}, epoch {})", e.epoch.player,
                  e.epoch.reason, e.epoch.epoch);
            } else if constexpr (std::is_same_v<E, tandem::events::Ended>) {
              Log("session ended: {}", e.reason);
              g_stop = 1;
            }
          },
          event);
    }
    if (auto relay = endpoint->Relay()) {
      relay->codeSecondsLeft = 0; // not a change worth a line
      if (relay != relayShown) {
        using State = tandem::net::RelayStatus::State;
        if (!relay->code.empty() && (!relayShown || relayShown->code != relay->code))
          Log("join code {} (relay {}:{})", tandem::net::DisplayJoinCode(relay->code),
              options.relay->address, options.relay->port);
        if (relay->state == State::Ready && (!relayShown || relayShown->state != State::Ready))
          Log("relay: registered{}", endpoint->ThroughRelay() ? ", joining through it" : "");
        if (!relay->error.empty() && (!relayShown || relayShown->error != relay->error))
          Log("relay: {}", relay->error);
        relayShown = relay;
      }
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
    // --sandevistan-at: on, then off.
    if (options.sandevistanAtSeconds >= 0 && sandevistanStep != PauseStep::Done) {
      if (!togetherSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
        togetherSince = now;
      const auto at = togetherSince
                          ? *togetherSince + std::chrono::seconds(options.sandevistanAtSeconds)
                          : std::chrono::steady_clock::time_point::max();
      if (sandevistanStep == PauseStep::Waiting && now >= at) {
        endpoint->SetLocalTimeEffect(
            {.abilities = tandem::proto::kTimeSandevistan, .scale = options.sandevistanScale});
        sandevistanStep = PauseStep::Paused;
        Log("sandevistan on: world speed {:.2f} around us for {} s", options.sandevistanScale,
            options.sandevistanForSeconds);
      } else if (sandevistanStep == PauseStep::Paused &&
                 now >= at + std::chrono::seconds(options.sandevistanForSeconds)) {
        endpoint->SetLocalTimeEffect({});
        sandevistanStep = PauseStep::Done;
        Log("sandevistan off");
      }
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
    // --story: the scripted host's steps, once a guest is in the session.
    if (options.host && storyNext < story.size() && endpoint->Players().size() > 1) {
      if (!storyStart)
        storyStart = now;
      while (storyNext < story.size() &&
             now >= *storyStart + std::chrono::milliseconds(
                                      static_cast<std::int64_t>(story[storyNext].at * 1000))) {
        const auto &step = story[storyNext++];
        if (auto message = StoryMessage(step, storyLine, endpoint->You())) {
          // A reward goes to every guest, one by one.
          if (std::holds_alternative<tandem::proto::QuestReward>(*message)) {
            for (const auto &[id, info] : endpoint->Players())
              if (id != endpoint->You())
                endpoint->SendStory(*message, id);
          } else
            endpoint->SendStory(*message);
          Log("story step: {} {}", step.verb, step.rest);
        } else
          Log("story step not understood: {} {}", step.verb, step.rest);
      }
      if (storyNext == story.size())
        Log("story: done");
    }
    // --answer / --reply: a guest acting on the host's calls and texts.
    if (!options.host && endpoint->IsJoined()) {
      if (!joinedAt)
        joinedAt = now;
      if (toAnswer && now >= toAnswer->second) {
        endpoint->SendStory(tandem::proto::PhoneAction{.action = tandem::proto::kPhoneAnswer,
                                                       .contact = toAnswer->first});
        Log("answer the call from {:#x}", toAnswer->first);
        toAnswer.reset();
      }
      if (options.replyEntry && !replied && *joinedAt + 3s <= now) {
        endpoint->SendStory(tandem::proto::PhoneReply{.choiceEntry = options.replyEntry});
        Log("reply {}", options.replyEntry);
        replied = true;
      }
    }
    // Quickhacks. Uploads whose time is up: the host's word that they are
    // done, our own effect landing.
    for (auto it = pendingHacks.begin(); it != pendingHacks.end();) {
      if (now < it->at) {
        ++it;
        continue;
      }
      if (it->effect) {
        endpoint->SendNpcEffect({.npc = it->state.npc, .effects = {it->effect}});
        Log("upload on npc {} done: effect {}", it->state.npc, options.hackEffect);
      } else {
        endpoint->SendNpcHackState(it->state);
        Log("npc {}: player {}'s upload done", it->state.npc, it->state.player);
      }
      it = pendingHacks.erase(it);
    }
    // --hack-npcs: upload on one of the host's NPCs, as the game would send it.
    if (options.hackNpcsSeconds && !options.host && endpoint->IsJoined() && !liveNpcs.empty() &&
        now >= nextHack) {
      nextHack = now + std::chrono::seconds(options.hackNpcsSeconds);
      const auto npc = *liveNpcs.begin();
      endpoint->SendNpcHack(
          {.npc = npc, .action = TweakDBID(options.hackAction), .uploadMs = 1500, .flags = 0});
      pendingHacks.push_back({now + 1500ms, {.npc = npc}, TweakDBID(options.hackEffect)});
      Log("upload {} on npc {} (1.5 s)", options.hackAction, npc);
    }
    // --hack-player: one of our NPCs hacks the first guest.
    if (options.hackPlayerSeconds && options.host && !replayedNpcs.empty() &&
        endpoint->Players().size() > 1 && now >= nextPlayerHack) {
      nextPlayerHack = now + std::chrono::seconds(options.hackPlayerSeconds);
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You()) {
          const auto npc = *replayedNpcs.begin();
          const bool trace = options.npcHack == "AIQuickHack.HackRevealPosition";
          endpoint->SendPlayerHack(
              {.player = id,
               .npc = npc,
               .action = TweakDBID(options.npcHack),
               .flags = trace ? tandem::proto::kPlayerHackReveal : std::uint8_t{0}});
          Log("npc {} hacks player {} with {}", npc, id, options.npcHack);
          break;
        }
    }
    // --hack-car: brake the car someone else drives.
    if (options.hackCarSeconds && endpoint->IsJoined() && now >= nextCarHack) {
      nextCarHack = now + std::chrono::seconds(options.hackCarSeconds);
      for (const auto &[player, drives] : driving)
        if (drives && player != endpoint->You()) {
          endpoint->SendVehicleHack(
              {.driver = player, .hack = tandem::proto::CarHack::ForceBrakes, .seconds = 3.f});
          Log("hack player {}'s car: brakes for 3 s", player);
        }
    }
    // --hit-you: one of our NPCs (a live replayed one, else none) shoots each
    // guest: an ordinary ranged hit (gamedataAttackType.Ranged = 10), no
    // attack record (the game falls back to a plain bullet).
    if (options.hitYouSeconds && options.host && now >= nextHitYou) {
      nextHitYou = now + std::chrono::seconds(options.hitYouSeconds);
      tandem::proto::NpcId shooter = 0;
      for (const auto npc : replayedNpcs)
        if (!deadNpcs.contains(npc)) {
          shooter = npc;
          break;
        }
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You() && endpoint->SendPlayerHit({.player = id,
                                                              .npc = shooter,
                                                              .physical = 10.f,
                                                              .attackType = 10,
                                                              .direction = {0.f, 1.f, 0.f}}))
          Log("npc {} hit player {} for 10 physical", shooter, id);
    }
    // Seconds since another player was first in the session with us.
    if (!sharedSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
      sharedSince = now;
    const double shared =
        sharedSince ? std::chrono::duration<double>(now - *sharedSince).count() : -1.0;
    // --detect-at: the host's NPCs notice the guests (2 s), have detected
    // them (--detect-for s), then lose them.
    if (options.host && options.detectAtSeconds >= 0 && shared >= 0.0) {
      const double at = options.detectAtSeconds;
      const int phase = shared < at                                    ? 0
                        : shared < at + 2.0                            ? 1
                        : shared < at + 2.0 + options.detectForSeconds ? 2
                                                                       : 3;
      if (phase != detectPhase) {
        detectPhase = phase;
        constexpr const char *kPhases[] = {"", "notice", "have detected", "lost"};
        Log("npcs {} the guests", kPhases[phase]);
      }
      if (now >= nextAwareness) {
        nextAwareness = now + 200ms;
        const auto level = phase == 1   ? static_cast<std::uint8_t>((shared - at) / 2.0 * 255.0)
                           : phase == 2 ? std::uint8_t{255}
                                        : std::uint8_t{0};
        tandem::proto::NpcAwareness awareness;
        auto npcs = sentNpcs;
        if (ownNpc)
          npcs.push_back(ownNpc->npc);
        for (const auto npc : npcs)
          for (const auto &[id, info] : endpoint->Players())
            if (id != endpoint->You() && level > 0 &&
                awareness.entries.size() < tandem::proto::kMaxNpcAwareness)
              awareness.entries.push_back({.npc = npc, .player = id, .level = level});
        endpoint->SendNpcAwareness(std::move(awareness));
      }
    }
    // --npc: one NPC standing 6 m in front of where the guest was first
    // heard, facing it, its states at 10 Hz (with the replay's, if any).
    if (options.host && !options.npcRecord.empty() && firstRemote && shared >= 0.0) {
      if (!ownNpc) {
        const float yaw = tandem::sync::YawOf(firstRemote->orientation);
        const Vec3 at{firstRemote->position.x - std::sin(yaw) * 6.f,
                      firstRemote->position.y + std::cos(yaw) * 6.f, firstRemote->position.z};
        ownNpc = tandem::proto::NpcEntry{
            .npc = kOwnNpc, .position = at, .yaw = yaw + std::numbers::pi_v<float>};
        endpoint->SendNpcSpawn({.npc = kOwnNpc,
                                .record = TweakDBID(options.npcRecord),
                                .position = at,
                                .yaw = ownNpc->yaw});
        Log("npc {}: {} in front of player {}", kOwnNpc, options.npcRecord, firstRemote->player);
      }
      ownNpc->detected = detectedBits();
      if (replay.empty() && now >= nextNpcState) {
        nextNpcState = now + 100ms;
        endpoint->SendNpcStates({.npcs = {*ownNpc}});
      }
    }
    // --camo: our optical camo on for 5 s.
    if (options.camoAtSeconds >= 0 && shared >= 0.0 && camoStep < 2) {
      if (camoStep == 0 && shared >= options.camoAtSeconds) {
        endpoint->SetLocalStealth({.flags = tandem::proto::kStealthCamo, .visibility = 0.2f});
        camoStep = 1;
        Log("optical camo on");
      } else if (camoStep == 1 && shared >= options.camoAtSeconds + 5) {
        endpoint->SetLocalStealth({.flags = 0, .visibility = 1.f});
        camoStep = 2;
        Log("optical camo off");
      }
    }
    // --shoot: a gunshot, seen (Shot) and heard (Noise, gamedataStimType.Gunshot 28).
    if (options.shootSeconds && !options.host && endpoint->IsJoined() && now >= nextShot) {
      nextShot = now + std::chrono::seconds(options.shootSeconds);
      endpoint->SendLocalShot({.hand = tandem::proto::Hand::Right, .effect = 16});
      endpoint->SendNoise({.stim = 28, .radius = 25.f});
      Log("gunshot");
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
          replayedNpcs.insert(spawn->npc);
        } else if (auto *states = std::get_if<tandem::proto::NpcStates>(&message)) {
          std::erase_if(states->npcs,
                        [&](const auto &entry) { return deadNpcs.contains(entry.npc); });
          sentNpcs.clear();
          for (auto &entry : states->npcs) {
            shift(entry.position);
            entry.detected |= detectedBits();
            sentNpcs.push_back(entry.npc);
          }
          if (ownNpc)
            states->npcs.push_back(*ownNpc);
          endpoint->SendNpcStates(*states);
        } else if (const auto *gone = std::get_if<tandem::proto::NpcGone>(&message)) {
          replayedNpcs.erase(gone->npc);
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
      if (const auto &faults = endpoint->FaultStats(); faults.Active())
        Log("faults: {} message(s) lost, {} sent twice; {} resync(s) asked of us so far",
            faults.Dropped(), faults.Duplicated(), resyncsAsked);
    }
  }

  endpoint->Leave("tandem-peer stopped");
  return 0;
}
