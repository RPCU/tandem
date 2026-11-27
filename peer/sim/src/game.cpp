#include "tandem/sim/game.hpp"
#include "tandem/sim/looks.hpp"

#include <algorithm>
#include <charconv>
#include <csignal>
#include <format>
#include <iostream>
#include <optional>
#include <thread>

namespace tandem::sim {
namespace {
namespace p = proto;
using namespace std::chrono_literals;

constexpr std::uint64_t kSecond = 1'000'000;

const Persona &PersonaOr(const std::string &name) {
  if (const auto *persona = PersonaNamed(name))
    return *persona;
  return *PersonaNamed("companion");
}

template <class... Ts> constexpr bool IsAny(const p::Message &message) {
  return (std::holds_alternative<Ts>(message) || ...);
}

volatile std::sig_atomic_t g_stop = 0;
} // namespace

Game::Game(GameOptions options, Coverage &coverage)
    : m_options(std::move(options)), m_coverage(coverage), m_persona(PersonaOr(m_options.persona)),
      m_random(m_options.seed),
      m_v(m_persona, {.host = m_options.host, .pace = m_options.pace, .look = m_options.look},
          m_random.Fork(), &coverage, &m_motion) {
  if (m_options.name.empty())
    m_options.name = std::format("{}-{}", m_persona.name, m_options.seed);
  std::string motionLog;
  m_motion.Load(m_options.playDir, motionLog);
  Say(motionLog);
  if (m_options.host) {
    HostWorld::Config config;
    config.difficulty = static_cast<std::uint8_t>(m_random.Index(4));
    m_world = std::make_unique<HostWorld>(config, m_random.Fork(), &coverage);
    m_world->hitOwnV = [this](const p::PlayerHit &hit) { m_ownHits.push_back(hit); };
    std::vector<StoryStep> steps;
    if (!m_options.storyFile.empty()) {
      std::string error;
      steps = LoadStory(m_options.storyFile, error);
      if (!error.empty())
        Say("sim: story: " + error + ": the default story instead");
    }
    if (steps.empty())
      steps = DefaultStory();
    m_story = std::make_unique<StoryDirector>(std::move(steps), &coverage);
  }
  m_coverage.Declare("session.rejoin");
  m_coverage.Declare("session.vanish");
  m_coverage.Declare("session.leave");
}

Game::~Game() {
  if (!m_stopped)
    Stop();
}

bool Game::Start(std::string &error) {
  if (!Connect(error))
    return false;
  Say(std::format("sim: {} as {} ({}), seed {}{}", m_options.host ? "hosting" : "joining",
                  m_options.name, m_persona.what, m_options.seed,
                  m_shaper ? std::format(", a bad link: {} ms, {:.0f}% lost",
                                         m_persona.link.latencyMs, m_persona.link.loss * 100)
                           : ""));
  return true;
}

bool Game::Connect(std::string &error) {
  const Identity identity{m_options.name, p::Body::Female};
  const bool shaped = m_options.shape && m_persona.link.Active();
  m_shaper.reset();
  if (m_options.host) {
    m_endpoint = Endpoint::Host(identity, shaped ? 0 : m_options.port, error, m_options.timing);
    if (m_endpoint && shaped) {
      m_shaper = net::Shaper::Start(m_options.port, "127.0.0.1", m_endpoint->LocalPort(),
                                    m_persona.link, error, m_options.seed);
      if (!m_shaper)
        m_endpoint.reset();
    }
  } else if (shaped) {
    m_shaper = net::Shaper::Start(0, m_options.address, m_options.port, m_persona.link, error,
                                  m_options.seed + m_sessions);
    if (m_shaper)
      m_endpoint = Endpoint::Join(identity, "127.0.0.1", m_shaper->Port(), error, m_options.timing);
  } else {
    m_endpoint =
        Endpoint::Join(identity, m_options.address, m_options.port, error, m_options.timing);
  }
  if (!m_endpoint)
    return false;
  m_referee = std::make_unique<Referee>(m_options.host);
  m_referee->Attach(*m_endpoint);
  ++m_sessions;
  m_scene = {};
  // What our V is, for the new connection (each kept by the endpoint).
  Outbox out;
  m_v.Introduce(out);
  for (const auto &outgoing : out)
    Deliver(*m_endpoint, outgoing);
  return true;
}

std::uint16_t Game::Port() const {
  if (m_shaper)
    return m_shaper->Port();
  return m_endpoint ? m_endpoint->LocalPort() : 0;
}

void Game::Retire() {
  if (!m_referee)
    return;
  m_referee->End();
  m_violations += m_referee->Violations();
  m_hints += m_referee->Hints();
  for (const auto &[rule, count] : m_referee->Counts())
    m_rules[rule] += count;
  m_coverage.Messages(m_referee->SentCounts(), m_referee->ReceivedCounts());
  for (const auto &finding : m_referee->Take()) {
    Say(finding.Line());
    if (!finding.hint && m_findings.size() < 50)
      m_findings.push_back(finding.Line());
  }
  m_referee.reset();
}

std::uint32_t Game::Violations() const {
  return m_violations + (m_referee ? m_referee->Violations() : 0);
}
std::uint32_t Game::Hints() const { return m_hints + (m_referee ? m_referee->Hints() : 0); }
std::map<std::string, std::uint32_t> Game::Rules() const {
  auto rules = m_rules;
  if (m_referee)
    for (const auto &[rule, count] : m_referee->Counts())
      rules[rule] += count;
  return rules;
}

void Game::Drop(bool goodbye, const std::string &reason, std::uint64_t nowUs) {
  if (goodbye && m_endpoint)
    m_endpoint->Leave(reason);
  Retire();
  // No goodbye: the endpoint goes as a killed game's does, without a word.
  m_endpoint.reset();
  m_shaper.reset();
  m_scene = {};
  m_rejoinAtUs = nowUs + static_cast<std::uint64_t>(m_random.Between(2.f, 4.f) * 1e6f);
  m_leaveAtUs = 0;
  m_npcsApartUs.reset();
  m_v.Rejoined(nowUs);
}

void Game::Stop(const std::string &reason) {
  if (m_stopped)
    return;
  m_stopped = true;
  if (m_endpoint)
    m_endpoint->Leave(reason);
  Retire();
  m_endpoint.reset();
  m_shaper.reset();
}

void Game::Flag(const std::string &what, std::uint64_t nowUs) {
  // The same finding is told once a minute.
  auto &last = m_flagged[what];
  if (last != 0 && nowUs - last < 60 * kSecond)
    return;
  last = nowUs;
  ++m_checkFailures;
  const auto line = "sim-check: " + what;
  Say(line);
  if (m_findings.size() < 50)
    m_findings.push_back(line);
}

void Game::OnEvent(const SessionEvent &event, std::uint64_t nowUs, Outbox &out) {
  std::visit(
      [&](const auto &e) {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<E, events::Joined>) {
          Say(std::format("sim: in the session as player {}, host {}", e.you, e.host));
          if (!m_options.host && m_persona.leaveEverySeconds > 0.)
            m_leaveAtUs =
                nowUs + static_cast<std::uint64_t>(m_persona.leaveEverySeconds / m_options.pace *
                                                   m_random.Between(0.7f, 1.3f) * 1e6);
        } else if constexpr (std::is_same_v<E, events::PlayerJoined>) {
          Say(std::format("sim: player {} ({}) joined", e.player.id, e.player.name));
        } else if constexpr (std::is_same_v<E, events::PlayerReturned>) {
          Say(std::format("sim: player {} ({}) is back", e.player.id, e.player.name));
        } else if constexpr (std::is_same_v<E, events::PlayerLeft>) {
          Say(std::format("sim: player {} left", e.player));
          if (m_world)
            m_world->OnPlayerLeft(e.player, nowUs, out);
        } else if constexpr (std::is_same_v<E, events::Ended>) {
          Say("sim: session ended: " + e.reason);
        } else if constexpr (std::is_same_v<E, events::Resync>) {
          Say(std::format("sim: resync of domains {:#x} {}", e.domains,
                          e.player == m_scene.self ? "asked" : "sent"));
        }
        // Regions (regions.md): the host's world gives, takes and lets go of
        // NPCs as the endpoint arbitrates. A simulated guest runs none: it
        // answers nothing (its scene keeps the proxy, which a refusal would
        // leave behind as the host drops the NPC).
        if (m_world) {
          if constexpr (std::is_same_v<E, events::NpcGive>) {
            p::NpcHandoverState state;
            if (m_world->Give(e.npc, e.to, state))
              m_endpoint->GiveNpc(std::move(state));
            else
              m_endpoint->RefuseNpc(e.npc);
          } else if constexpr (std::is_same_v<E, events::NpcTake>) {
            const auto &shared = m_endpoint->SharedNpcs();
            const auto spawn = shared.find(e.state.npc);
            if (spawn != shared.end())
              m_world->Adopt(e.state, spawn->second, nowUs);
            m_endpoint->AdoptNpc(e.state.npc, spawn != shared.end());
          } else if constexpr (std::is_same_v<E, events::NpcOwnerChanged>) {
            if (e.was == m_scene.self)
              m_world->Release(e.npc);
          }
        }
        if (!m_world)
          return;
        // The host's world answers what guests do to it.
        if constexpr (std::is_same_v<E, events::NpcHitBy>) {
          m_world->OnStrike({.player = e.hit.player,
                             .npc = e.hit.npc,
                             .damage = e.hit.damage,
                             .position = e.hit.position,
                             .direction = e.hit.direction},
                            nowUs, out);
        } else if constexpr (std::is_same_v<E, events::Combat>) {
          if (const auto *strike = std::get_if<p::NpcStrike>(&e.message))
            m_world->OnStrike(*strike, nowUs, out);
        } else if constexpr (std::is_same_v<E, events::NpcEffectBy>) {
          m_world->OnEffect(e.effect, nowUs, out);
        } else if constexpr (std::is_same_v<E, events::NpcHackBy>) {
          m_world->OnHack(e.hack, nowUs, out);
        } else if constexpr (std::is_same_v<E, events::NpcActionBy>) {
          m_world->OnAction(e.action, nowUs, m_scene, m_v.Self(), out);
        } else if constexpr (std::is_same_v<E, events::NoiseBy>) {
          m_world->OnNoise(e.noise, nowUs, m_scene, m_v.Self());
        } else if constexpr (std::is_same_v<E, events::ShotHitBy>) {
          m_world->OnShotHit(e.hit);
        } else if constexpr (std::is_same_v<E, events::Story>) {
          m_story->On(event);
        }
      },
      event);
}

void Game::Route(Outbox &out, std::uint64_t nowUs) {
  std::deque<Outgoing> queue(std::make_move_iterator(out.begin()),
                             std::make_move_iterator(out.end()));
  out.clear();
  const auto self = m_endpoint->You();
  while (!queue.empty()) {
    auto outgoing = std::move(queue.front());
    queue.pop_front();
    auto &message = outgoing.message;
    if (m_world) {
      // A host's own V acts on its NPCs without a message.
      Outbox more;
      bool handled = true;
      if (auto *strike = std::get_if<p::NpcStrike>(&message)) {
        strike->player = self;
        m_world->OnStrike(*strike, nowUs, more);
      } else if (auto *effect = std::get_if<p::NpcEffect>(&message)) {
        effect->player = self;
        m_world->OnEffect(*effect, nowUs, more);
      } else if (auto *hack = std::get_if<p::NpcHack>(&message)) {
        hack->player = self;
        m_world->OnHack(*hack, nowUs, more);
      } else if (auto *action = std::get_if<p::NpcAction>(&message)) {
        action->player = self;
        m_world->OnAction(*action, nowUs, m_scene, m_v.Self(), more);
      } else if (auto *noise = std::get_if<p::Noise>(&message)) {
        noise->player = self;
        m_world->OnNoise(*noise, nowUs, m_scene, m_v.Self());
      } else if (std::holds_alternative<p::ShotHit>(message)) {
        // (a host's NPCs never fire at it through the wire)
      } else if (const auto *reply = std::get_if<p::NpcActionReply>(&message);
                 reply && outgoing.to == self) {
        Outbox back;
        m_v.Heard(*reply, nowUs, m_scene, back);
        more.insert(more.end(), back.begin(), back.end());
      } else if (std::holds_alternative<p::PhoneAction>(message)) {
        // A host has no host to call.
      } else {
        handled = false;
      }
      if (handled) {
        for (auto &next : more)
          queue.push_back(std::move(next));
        continue;
      }
      // Our world's word our V hears too: its uploads, its poise.
      if (IsAny<p::NpcHackState, p::NpcPoise, p::Elsewhere>(message)) {
        Outbox back;
        m_v.Heard(message, nowUs, m_scene, back);
        for (auto &next : back)
          queue.push_back(std::move(next));
      }
    }
    if (!Deliver(*m_endpoint, outgoing) && !m_undeliverable[message.index()]) {
      m_undeliverable[message.index()] = true;
      Say(std::format("sim: {} has no endpoint call as {}", p::NameOf(message),
                      m_options.host ? "host" : "guest"));
    }
  }
}

void Game::Check(std::uint64_t nowUs) {
  if (!m_endpoint || !m_endpoint->IsJoined())
    return;
  if (m_world) {
    // The world and the session's record of it, every second.
    if (const auto apart = m_world->Check(*m_endpoint); !apart.empty())
      Flag(apart, nowUs);
    return;
  }
  // A guest: the NPCs its game shows (the scene, from the events) and the
  // session's record must agree, once a resync has had time to heal them.
  std::string apart;
  const auto &shared = m_endpoint->SharedNpcs();
  for (const auto &[id, spawn] : shared)
    if (const auto it = m_scene.npcs.find(id); it == m_scene.npcs.end() || !it->second.live) {
      apart = std::format("npc {} is shared and the game shows no live proxy of it", id);
      break;
    }
  if (apart.empty())
    for (const auto &[id, npc] : m_scene.npcs)
      if (npc.live && !shared.contains(id)) {
        apart = std::format("npc {} has a live proxy and is not shared", id);
        break;
      }
  if (apart.empty()) {
    m_npcsApartUs.reset();
  } else if (!m_npcsApartUs) {
    m_npcsApartUs = nowUs;
  } else if (nowUs - *m_npcsApartUs > 15 * kSecond) {
    Flag(apart, nowUs);
  }
}

void Game::Step(std::chrono::milliseconds wait) {
  if (m_stopped)
    return;
  auto nowUs = net::NowUs();
  if (!m_endpoint) {
    if (m_rejoinAtUs == 0 || nowUs < m_rejoinAtUs) {
      std::this_thread::sleep_for(wait);
      return;
    }
    std::string error;
    if (!Connect(error)) {
      Say("sim: could not join again: " + error);
      m_rejoinAtUs = nowUs + 5 * kSecond;
      return;
    }
    m_coverage.Hit("session.rejoin");
    Say("sim: joining again");
  }
  Outbox out;
  bool ended = false;
  for (const auto &event : m_endpoint->Update(wait)) {
    nowUs = net::NowUs();
    m_scene.On(event, nowUs);
    OnEvent(event, nowUs, out);
    m_v.On(event, nowUs, m_scene, out);
    ended = ended || std::holds_alternative<events::Ended>(event);
  }
  if (ended) {
    Route(out, nowUs);
    // A guest's game tries again, as the plugin does.
    Drop(false, "", nowUs);
    m_rejoinAtUs = nowUs + 3 * kSecond;
    return;
  }
  nowUs = net::NowUs();
  if (m_endpoint->IsJoined()) {
    m_scene.self = m_endpoint->You();
    m_scene.host = m_endpoint->HostPlayer();
    for (const auto &hit : std::exchange(m_ownHits, {}))
      m_v.HitBy(hit, nowUs, m_scene, out);
    if (m_world) {
      m_world->Fill(m_scene);
      m_world->Tick(nowUs, m_scene, m_v.Self(), out);
      m_story->Tick(nowUs, m_scene, m_v.Self(), out);
      m_world->Fill(m_scene);
    }
    m_v.Tick(nowUs, m_scene, out);
    if (m_world && m_v.Rebuilt())
      m_world->Rebuild(nowUs, out);
    Route(out, nowUs);
    if (m_v.Doing() != m_shownGoal) {
      m_shownGoal = m_v.Doing();
      Say(std::format("sim: v {} (health {:.0f})", NameOf(m_shownGoal), m_v.Health()));
    }
  }
  if (m_referee) {
    m_referee->Tick();
    for (const auto &finding : m_referee->Take()) {
      Say(finding.Line());
      if (!finding.hint && m_findings.size() < 50)
        m_findings.push_back(finding.Line());
    }
  }
  if (nowUs >= m_nextCheckUs) {
    m_nextCheckUs = nowUs + kSecond;
    Check(nowUs);
  }
  // Chaos leaves now and then, half the time with no goodbye.
  if (m_leaveAtUs != 0 && nowUs >= m_leaveAtUs && Others()) {
    const bool vanish = m_random.Chance(m_persona.vanishShare);
    m_coverage.Hit(vanish ? "session.vanish" : "session.leave");
    Say(vanish ? "sim: vanishing (no goodbye)" : "sim: leaving, to join again");
    Drop(!vanish, "chaos leaves", nowUs);
  }
}

std::string Game::Summary() const {
  std::string rules;
  for (const auto &[rule, count] : Rules())
    rules += std::format(" {} x{}", rule, count);
  return std::format("sim summary: {} ({}), {} session(s), {} violation(s), {} hint(s), {} failed "
                     "check(s){}{}",
                     m_options.name, m_options.host ? "host" : "guest", m_sessions, Violations(),
                     Hints(), m_checkFailures, rules.empty() ? "" : ":", rules);
}

PairResult RunPair(const PairOptions &options, Coverage &coverage) {
  PairResult result;
  const auto prefixed = [&](std::string prefix) -> std::function<void(const std::string &)> {
    if (!options.log)
      return {};
    return [&, prefix](const std::string &line) { options.log(prefix + line); };
  };
  Game host({.host = true,
             .persona = options.hostPersona,
             .seed = options.seed,
             .port = 0,
             .timing = options.timing,
             .storyFile = options.storyFile,
             .playDir = options.playDir,
             .pace = options.pace,
             .log = prefixed("host: ")},
            coverage);
  std::string error;
  if (!host.Start(error)) {
    result.findings.push_back("sim-check: the host did not start: " + error);
    ++result.checkFailures;
    return result;
  }
  Game guest({.host = false,
              .persona = options.guestPersona,
              .seed = options.seed * 7919 + 1,
              .address = "127.0.0.1",
              .port = host.Port(),
              .timing = options.timing,
              .playDir = options.playDir,
              .pace = options.pace,
              .log = prefixed("guest: ")},
             coverage);
  if (!guest.Start(error)) {
    result.findings.push_back("sim-check: the guest did not start: " + error);
    ++result.checkFailures;
    return result;
  }
  const auto end =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(options.seconds);
  while (std::chrono::steady_clock::now() < end && !g_stop) {
    host.Step(1ms);
    guest.Step(1ms);
    result.joined = result.joined || (guest.Joined() && host.Others());
  }
  guest.Stop();
  // The host hears the goodbye before it goes.
  for (int i = 0; i < 20; ++i)
    host.Step(2ms);
  host.Stop();
  if (options.log) {
    options.log("host: " + host.Summary());
    options.log("guest: " + guest.Summary());
  }
  result.violations = host.Violations() + guest.Violations();
  result.hints = host.Hints() + guest.Hints();
  result.checkFailures = host.CheckFailures() + guest.CheckFailures();
  result.guestSessions = guest.Sessions();
  for (const auto *game : {&host, &guest}) {
    for (const auto &[rule, count] : game->Rules())
      result.rules[rule] += count;
    const auto findings = game->Findings();
    result.findings.insert(result.findings.end(), findings.begin(), findings.end());
  }
  return result;
}

// ---- tandem-peer sim, sim-fuzz ----
namespace {
template <class... Args> void Log(std::format_string<Args...> format, Args &&...args) {
  std::cout << std::format("[{:>8.3f}] ", net::NowUs() % 100'000'000 / 1e6)
            << std::format(format, std::forward<Args>(args)...) << '\n'
            << std::flush;
}

int SimUsage(std::string_view error) {
  if (!error.empty())
    std::cerr << "tandem-peer: " << error << "\n\n";
  std::cerr << "usage: tandem-peer sim --role host|join [ADDRESS[:PORT]] [options]\n"
               "       tandem-peer sim-fuzz [--seeds K] [--first N] [--seconds S] [options]\n\n"
               "  --role R           host: the game joins us; join: we join ADDRESS (default\n"
               "                     127.0.0.1)\n"
               "  --persona P        companion | loner | chaos | speedrunner | partner (default "
               "companion)\n"
               "  --seed N           the run's seed (default 1): the same seed, the same run\n"
               "  --minutes M        how long (default 5; sim-fuzz: --seconds per pair, 60)\n"
               "  --port N           UDP port (default 27450)\n"
               "  --name NAME        the player's name (default PERSONA-SEED)\n"
               "  --story FILE       host: the story to play (default: the sim's own quest)\n"
               "  --play-dir DIR     real play to stitch the V's moves from (default\n"
               "                     recordings/play)\n"
               "  --pace X           goals, leaves and waits X times faster (default 1)\n"
               "  --body B           sim: female | male, a real V's body from --look-keys in\n"
               "                     random clothes (default: male when male.keys is saved,\n"
               "                     else female)\n"
               "  --look-keys DIR    where female.keys / male.keys are kept (default\n"
               "                     ~/.cache/tandem/looks; tandem-peer saves them)\n"
               "  --no-link          no bad link, whatever the persona's\n"
               "  --seeds K          sim-fuzz: K pairs (default 10), personas by seed, from\n"
               "                     --first N (default 1)\n"
               "  --pair HOST:GUEST  sim-fuzz: these personas for every seed\n"
               "  --quiet            sim-fuzz: only each pair's result and what it found\n";
  return 2;
}

template <class T> std::optional<T> NumberOf(std::string_view text) {
  T value{};
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return value;
}
} // namespace

int SimMain(std::string_view command, const std::vector<std::string_view> &args) {
  GameOptions options;
  double minutes = 5., seconds = 60.;
  std::uint64_t seeds = 10, first = 1;
  bool roleGiven = false, quiet = false;
  std::string pairHost, pairGuest; // sim-fuzz --pair HOST:GUEST
  std::optional<proto::Body> body; // sim --body
  std::string lookKeys;            // sim --look-keys
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto flag = args[i];
    if (flag == "--help" || flag == "-h")
      return SimUsage("");
    if (flag == "--no-link") {
      options.shape = false;
      continue;
    }
    if (flag == "--quiet") {
      quiet = true;
      continue;
    }
    if (!flag.starts_with("--")) {
      auto target = flag;
      if (const auto colon = target.rfind(':'); colon != std::string_view::npos) {
        const auto port = NumberOf<std::uint16_t>(target.substr(colon + 1));
        if (!port)
          return SimUsage(std::format("not a port: {}", target));
        options.port = *port;
        target = target.substr(0, colon);
      }
      options.address = std::string(target);
      continue;
    }
    if (i + 1 >= args.size())
      return SimUsage(std::format("{} needs a value", flag));
    const auto value = args[++i];
    const auto number = [&]<class T>(T &into) {
      const auto parsed = NumberOf<T>(value);
      if (!parsed)
        return false;
      into = *parsed;
      return true;
    };
    bool ok = true;
    if (flag == "--role") {
      roleGiven = value == "host" || value == "join";
      options.host = value == "host";
      ok = roleGiven;
    } else if (flag == "--persona") {
      options.persona = std::string(value);
      ok = PersonaNamed(value) != nullptr;
    } else if (flag == "--seed") {
      ok = number(options.seed);
    } else if (flag == "--minutes") {
      ok = number(minutes);
    } else if (flag == "--seconds") {
      ok = number(seconds);
    } else if (flag == "--port") {
      ok = number(options.port);
    } else if (flag == "--name") {
      options.name = std::string(value);
    } else if (flag == "--story") {
      options.storyFile = std::string(value);
    } else if (flag == "--play-dir") {
      options.playDir = std::string(value);
    } else if (flag == "--body") {
      ok = value == "female" || value == "male";
      body = value == "male" ? proto::Body::Male : proto::Body::Female;
    } else if (flag == "--look-keys") {
      lookKeys = std::string(value);
    } else if (flag == "--pace") {
      ok = number(options.pace) && options.pace > 0.f;
    } else if (flag == "--seeds") {
      ok = number(seeds);
    } else if (flag == "--first") {
      ok = number(first);
    } else if (flag == "--pair") {
      const auto colon = value.find(':');
      pairHost = std::string(value.substr(0, colon));
      pairGuest = colon == std::string_view::npos ? pairHost : std::string(value.substr(colon + 1));
      ok = PersonaNamed(pairHost) && PersonaNamed(pairGuest);
    } else {
      return SimUsage(std::format("unknown option {}", flag));
    }
    if (!ok)
      return SimUsage(std::format("{}: not a valid value: {}", flag, value));
  }
  std::signal(SIGINT, [](int) { g_stop = 1; });
  std::signal(SIGTERM, [](int) { g_stop = 1; });

  if (command == "sim-fuzz") {
    const auto names = PersonaNames();
    Coverage coverage;
    std::uint32_t failed = 0;
    for (std::uint64_t seed = first; seed < first + seeds && !g_stop; ++seed) {
      // Every pairing of personas comes round, each seed its own.
      const auto &hostPersona = pairHost.empty() ? names[seed % names.size()] : pairHost;
      const auto &guestPersona =
          pairGuest.empty() ? names[(seed / names.size() + seed) % names.size()] : pairGuest;
      PairOptions pair{.hostPersona = hostPersona,
                       .guestPersona = guestPersona,
                       .seed = seed,
                       .seconds = seconds,
                       .pace = options.pace,
                       .playDir = options.playDir,
                       .storyFile = options.storyFile};
      if (!quiet)
        pair.log = [](const std::string &line) { Log("{}", line); };
      Coverage run;
      const auto result = RunPair(pair, run);
      coverage.Merge(run);
      const bool ok = result.joined && result.violations == 0 && result.checkFailures == 0;
      failed += ok ? 0 : 1;
      Log("sim-fuzz: seed {} host {} guest {}: {} ({} violation(s), {} hint(s), {} failed "
          "check(s), {} guest session(s){})",
          seed, hostPersona, guestPersona, ok ? "ok" : "FAILED", result.violations, result.hints,
          result.checkFailures, result.guestSessions, result.joined ? "" : ", never joined");
      for (const auto &finding : result.findings)
        Log("sim-fuzz:   {}", finding);
    }
    for (const auto &line : coverage.Report())
      Log("{}", line);
    Log("sim-fuzz: {} of {} seed(s) failed", failed, seeds);
    return failed == 0 ? 0 : 1;
  }

  if (!roleGiven)
    return SimUsage("sim needs --role host or --role join");
  // Our V's look: a real V's body keys (saved by tandem-peer from a look
  // heard) in random clothes; without keys the game puts a
  // stand-in body on the puppet, and says so.
  const auto keysDir = LookKeysDir(lookKeys);
  const auto worn = body ? *body : SavedBody(keysDir).value_or(proto::Body::Female);
  const auto keysFile = LookKeysFile(keysDir, worn);
  std::string picked;
  // The clothes by persona, not by seed: the same V at every rejoin
  // (tools/play-session.sh counts its runs in --seed).
  options.look = RandomLook(worn, static_cast<unsigned>(CName(options.persona)),
                            LoadLookKeys(keysFile), {}, &picked);
  if (options.look.look.empty())
    Log("sim: look: {} body, no keys in {} (the game shows a stand-in body):{}",
        worn == proto::Body::Male ? "male" : "female", keysFile.string(), picked);
  else
    Log("sim: look: {} body, {} keys from {}:{}", worn == proto::Body::Male ? "male" : "female",
        options.look.look.size(), keysFile.string(), picked);
  Coverage coverage;
  options.log = [](const std::string &line) { Log("{}", line); };
  Game game(options, coverage);
  std::string error;
  if (!game.Start(error)) {
    std::cerr << "tandem-peer: " << error << '\n';
    return 1;
  }
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(minutes * 60.);
  while (!g_stop && std::chrono::steady_clock::now() < end)
    game.Step(5ms);
  game.Stop("tandem-peer stopped");
  for (const auto &line : coverage.Report())
    Log("{}", line);
  Log("{}", game.Summary());
  return game.Violations() == 0 && game.CheckFailures() == 0 ? 0 : 1;
}
} // namespace tandem::sim
