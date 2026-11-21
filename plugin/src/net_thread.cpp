#include "net_thread.hpp"

#include "activity.hpp"
#include "combat.hpp"
#include "comms.hpp"
#include "log.hpp"
#include "mods.hpp"
#include "perf.hpp"
#include "presence.hpp"
#include "recording.hpp"
#include "regions.hpp"
#include "runtime.hpp"
#include "stall.hpp"
#include "story.hpp"
#include "trade.hpp"

#include "tandem/combat.hpp"
#include "tandem/compat.hpp"
#include "tandem/consistency.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/net_traffic.hpp"
#include "tandem/play.hpp"
#include "tandem/report.hpp"
#include "tandem/traffic.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <thread>
#include <utility>

namespace tandem::plugin {
using namespace std::chrono_literals;

// Our V's state and pose wait this long at most for each other (one datagram).
constexpr std::uint64_t kCoalesceUs = 8'000;

// The session's record of the street, for the scripts: every shared
// vehicle, the ones we show as proxies, who runs each (traffic.md).
void ShareStreet(Runtime &runtime, const Endpoint &endpoint) {
  std::map<proto::TrafficId, proto::PlayerId> owners;
  for (const auto &[id, spawn] : endpoint.SharedTraffic())
    owners[id] = endpoint.TrafficOwnerOf(id);
  runtime.SetSharedTraffic(endpoint.SharedTraffic(), endpoint.TrafficStatuses(),
                           endpoint.ProxiedTraffic(), std::move(owners));
}

struct NetThread::Impl {
  Config config;
  proto::Body body{};
  std::jthread thread;
  // What we last handed an endpoint that it keeps for the session: a new
  // session (a guest joining again) starts from them.
  std::optional<proto::Appearance> lastAppearance;
  std::optional<proto::Held> lastHeld;
  // Who may drive our cars (said once by the scripts): again for each session.
  std::optional<proto::CarLend> lastLend;
  std::optional<proto::Seat> lastSeat;
  std::optional<proto::VehicleRadio> lastRadio;
  std::optional<proto::Stealth> lastStealth;
  bool lastPaused{};
  std::optional<proto::TimeEffect> lastTime;
  std::optional<proto::Transit> lastTransit;
  std::optional<proto::Heat> lastHeat;
  std::optional<proto::Elsewhere> lastElsewhere;
  std::optional<proto::Activity> lastActivity;
  // Guest through a relay: the session's secret, to join again after the
  // code expired.
  std::optional<net::RelaySecret> relaySecret;
  // ...and the relay replica holding the session ("ip:port"): the secret
  // does not say which one of a fleet it is.
  std::string relayAt;
  // Consistency: what the player is told (never twice in a short while),
  // and whether our last session was lost (to say we are back).
  consistency::NoticeGate notices;

  // Gameplay recording (tandem/play.hpp): our V as this game sends it, in a
  // file next to the DLL. Kept across sessions; tapped on each endpoint.
  play::Writer recorder;
  std::string recordingName;

  void Tap(Endpoint &endpoint) {
    endpoint.SetTap([this, e = &endpoint](const proto::Message &message) {
      recorder.Add(e->HostNowUs(), message);
    });
  }

  // Starts a recording named `name` ("": stops the one going).
  void Record(const std::string &name, Endpoint &endpoint) {
    if (recorder.IsOpen()) {
      endpoint.SetTap({});
      log::Info("recording: {} stopped, {} messages, {:.1f} s ({})", recordingName,
                recorder.Count(), static_cast<double>(recorder.LastUs()) / 1e6,
                recorder.Path().string());
      recorder.Close();
    }
    if (name.empty())
      return;
    const auto path = config.commandFile.parent_path() / "recordings" / (name + ".tplay");
    std::string error;
    if (!recorder.Open(
            path, {.startHostUs = endpoint.HostNowUs(), .player = endpoint.You(), .body = body},
            error)) {
      log::Warn("recording: {}", error);
      return;
    }
    recordingName = name;
    // What the others already know of our V (look, held items, seat...).
    for (const auto &message : endpoint.LocalSnapshot())
      recorder.Add(endpoint.HostNowUs(), message);
    Tap(endpoint);
    log::Info("recording: {} started ({})", name, path.string());
  }

  // Something serious, on screen (System.reds shows it the game's way), at
  // most once per `gapUs` for each key.
  void Notice(std::string_view key, std::string text, std::uint64_t gapUs = 60'000'000) {
    if (!notices.Allow(key, net::NowUs(), gapUs))
      return;
    log::Info("notice: {}", text);
    Runtime::Get().PushEvent({ScriptEvent::Kind::Notice, 0, std::move(text), {}});
  }

  static std::string Domains(std::uint8_t domains) {
    std::string out;
    for (const auto &[bit, name] : {std::pair{proto::kDomainWorld, "world"},
                                    {proto::kDomainNpcs, "npcs"},
                                    {proto::kDomainStory, "story"},
                                    {proto::kDomainPlayers, "players"},
                                    {proto::kDomainDevices, "devices"}})
      if (domains & bit)
        out += out.empty() ? name : std::string(", ") + name;
    return out.empty() ? "nothing" : out;
  }

  static const char *EpochReason(std::uint8_t reason) {
    switch (reason) {
    case proto::kEpochLoading:
      return "loading screen";
    case proto::kEpochFastTravel:
      return "fast travel";
    case proto::kEpochTimeSkip:
      return "time skip";
    case proto::kEpochFrozen:
      return "back from a standstill";
    default:
      return "join";
    }
  }

  // Once in the session, joining again goes by its secret alone: the code
  // has likely expired, and a host that restarted has a new code anyway (a
  // stale code would only spend the relay's lookups).
  std::optional<net::RelayOptions> RelayOf() const {
    if (config.relay.empty())
      return std::nullopt;
    net::RelayOptions options{.address = config.relay,
                              .port = config.relayPort,
                              .more = config.relayMore,
                              .token = config.relayToken,
                              .secret = relaySecret,
                              .code = relaySecret ? std::string() : config.joinCode,
                              .direct = Runtime::Get().directOn,
                              .keepCode = config.keepCode,
                              .codeSalt = Runtime::Get().codeSalt};
    if (relaySecret && !relayAt.empty()) {
      net::ParseRelayAddress(relayAt, options.address, options.port);
      options.more.clear(); // where the session is: no other to ask
    }
    return options;
  }

  // The relay link's state for scripts (Runtime::RelayStatus).
  static std::string StatusText(const net::RelayStatus &status) {
    if (status.state == net::RelayStatus::State::Ready)
      return "ready";
    return status.error.empty() ? "connecting" : status.error;
  }

  std::optional<proto::Vitals> lastVitals;
  std::optional<proto::Presence> lastPresence;
  std::optional<std::vector<std::uint64_t>> lastStatusVisuals;

  // Every other player in the session with its life (up unless it said
  // otherwise), for the game thread (Revive.reds). Cheap: the runtime keeps
  // it only when it changed.
  static void PublishLives(const Endpoint &endpoint) {
    std::vector<proto::Vitals> others;
    for (const auto &[id, info] : endpoint.Players()) {
      if (id == endpoint.You())
        continue;
      const auto it = endpoint.Lives().find(id);
      others.push_back(it != endpoint.Lives().end() ? it->second : proto::Vitals{.player = id});
      others.back().revision = 0; // only life and flags matter to the scripts
    }
    Runtime::Get().SetOthersLives(std::move(others));
  }

  // The other players in their pause menu, for the game thread.
  static void PublishPaused(const Endpoint &endpoint) {
    std::vector<proto::PlayerId> others;
    for (const auto player : endpoint.Paused())
      if (player != endpoint.You())
        others.push_back(player);
    Runtime::Get().SetRemotePaused(std::move(others));
  }

  // What the co-op menu shows: the state, and everyone with their ping.
  void Publish(SessionStatus::State state, const std::string &detail,
               const Endpoint *endpoint = nullptr) {
    SessionStatus status{.state = state, .detail = detail};
    if (endpoint) {
      status.you = endpoint->You();
      status.host = endpoint->HostPlayer();
      for (const auto &[id, info] : endpoint->Players()) {
        const auto link = endpoint->LinkStatsOf(id);
        status.players.push_back(
            {id, info.name, link ? static_cast<std::int32_t>(link->rttMs) : -1});
      }
    }
    Runtime::Get().SetStatus(std::move(status));
  }

  // A guest whose session ends (host gone, unreachable, timed out) tries
  // again every 5 s for as long as the game runs: a host that crashed or
  // restarted is joined again without touching the guest's game. A join
  // code that never led into a session (wrong, expired) is tried 3 times:
  // each try costs one of the relay's limited code lookups.
  void Run(std::stop_token stop) {
    // The stall report (stall.hpp) watches this thread while it runs.
    struct Gone {
      ~Gone() { stall::NetGone(); }
    } gone;
    for (int codeTries = 1;; ++codeTries) {
      stall::NetBeat(stall::kSession);
      const auto ended = Session(stop);
      if (stop.stop_requested())
        return;
      if (config.role != Config::Role::Join) {
        Publish(SessionStatus::State::Failed, ended);
        return;
      }
      // Another tandem release than the host's, or no Phantom Liberty on
      // one side: trying again changes nothing.
      if (compat::IsFinalRefusal(ended)) {
        log::Warn("not joining again: {}", ended);
        Publish(SessionStatus::State::Failed, ended);
        return;
      }
      if (!config.joinCode.empty() && !relaySecret && codeTries >= 3) {
        log::Warn("not joining again: code {} led to no session",
                  net::DisplayJoinCode(config.joinCode));
        Publish(SessionStatus::State::Failed, "the join code led to no session");
        return;
      }
      Publish(SessionStatus::State::Retrying, ended);
      for (int i = 0; i < 50 && !stop.stop_requested(); ++i) {
        stall::NetBeat(stall::kRetry);
        std::this_thread::sleep_for(100ms);
      }
      if (stop.stop_requested())
        return;
      log::Info("joining again");
    }
  }

  // Runs one session; answers why it ended.
  std::string Session(std::stop_token stop) {
    auto &runtime = Runtime::Get();
    // Nothing queued for or from the last session reaches this one.
    runtime.ResetSession();
    Regions::Get().Reset();
    std::string error;
    // (Without Phantom Liberty no session starts, unless compat = off: then
    // the other side refuses us, in words.)
    const Identity identity{.name = config.name,
                            .body = body,
                            .install = runtime.installation.install,
                            .mods = ModRecords::Get().OursForIdentity(),
                            .tellsMods = true,
                            .expansion = runtime.Expansion() != compat::Expansion::Disabled &&
                                         runtime.Expansion() != compat::Expansion::Missing};
    ModRecords::Get().Ended();
    runtime.ClearDifferences();
    report::SessionSummary summary{
        .build = runtime.installation.install.build,
        .role = NameOf(config.role),
        .startedUtc = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(
                                                              std::chrono::system_clock::now()))};
    const auto startedAt = std::chrono::steady_clock::now();
    const auto relay = RelayOf();
    // A guest with a join code goes through the relay, after the address
    // when tandem.ini gives one; without a code, to the address only.
    const bool byCode = !config.joinCode.empty();
    const JoinTarget target{.address = !byCode || config.addressSet ? config.address : "",
                            .port = config.port,
                            .relay = byCode ? relay : std::nullopt};
    std::unique_ptr<Endpoint> endpoint;
    if (config.role == Config::Role::Host)
      endpoint = Endpoint::Host(identity, config.port, error, {}, relay);
    else if (byCode && !relay)
      error = "a join code needs a relay (relay = host:port in tandem.ini)";
    else
      endpoint = Endpoint::Join(identity, target, error);
    if (!endpoint) {
      log::Error("could not start the session: {}", error);
      runtime.PushEvent({ScriptEvent::Kind::SessionEnded, 0, error, {}});
      return error;
    }
    Publish(config.role == Config::Role::Host ? SessionStatus::State::InSession
                                              : SessionStatus::State::Connecting,
            {}, endpoint.get());
    std::string endReason, refusal;
    bool wasIn = false; // hosting, or welcomed by a host
    if (recorder.IsOpen())
      Tap(*endpoint);
    if (runtime.config.sceneKbps != 0)
      endpoint->SetSceneBudget(runtime.config.sceneKbps * 125);
    // The host's level_gap (progress::SessionLevelOf), as this session began.
    endpoint->SetLevelGap(config.levelGap);
    // The recorder (recording.hpp): everything this endpoint says. Before
    // the repeats below, so a segment starts with our V's look and seat.
    if (Recording::Get().Enabled())
      endpoint->SetRecordTap([](const proto::Message &message) { Recording::Get().Add(message); });
    if (lastAppearance)
      endpoint->SetLocalAppearance(*lastAppearance);
    if (lastHeld)
      endpoint->SetLocalHeld(*lastHeld);
    if (lastLend)
      endpoint->SendCombat(*lastLend);
    if (lastSeat)
      endpoint->SetLocalSeat(*lastSeat);
    if (lastRadio)
      endpoint->SetLocalVehicleRadio(*lastRadio);
    if (lastStealth)
      endpoint->SetLocalStealth(*lastStealth);
    if (lastPaused)
      endpoint->SetLocalPaused(true);
    if (lastTime)
      endpoint->SetLocalTimeEffect(*lastTime);
    if (lastTransit)
      endpoint->SetLocalTransit(*lastTransit);
    if (lastHeat)
      endpoint->SetLocalHeat(*lastHeat);
    if (lastElsewhere)
      endpoint->SetLocalElsewhere(*lastElsewhere);
    if (lastActivity)
      endpoint->SetLocalActivity(*lastActivity);
    if (lastVitals)
      endpoint->SetLocalVitals(*lastVitals);
    if (lastPresence)
      endpoint->SetLocalPresence(*lastPresence);
    if (lastStatusVisuals)
      endpoint->SetLocalStatusVisuals(*lastStatusVisuals);
    // Comms: whom we share tags with, and our waypoint, from the start.
    endpoint->SetLocalLink(Comms::Get().OwnLink());
    if (const auto waypoint = Comms::Get().OwnWaypoint())
      endpoint->SetLocalWaypoint(*waypoint);
    if (config.role == Config::Role::Host)
      log::Info("hosting on UDP {}{}", config.port,
                relay ? std::format(", and through relay {}:{}", relay->address, relay->port) : "");
    else if (target.address.empty())
      log::Info("joining code {} through relay {}:{}", net::DisplayJoinCode(config.joinCode),
                config.relay, config.relayPort);
    else
      log::Info("joining {}:{}{}", config.address, config.port,
                target.relay ? " (through the relay if it does not answer)" : "");
    // The relay as last logged and handed to scripts.
    std::optional<net::RelayStatus> relayShown;
    // What this session's relay link was told last (it starts from RelayOf).
    bool directApplied = runtime.directOn;
    std::uint32_t newCodesAsked = runtime.newCodeRequests.load();

    auto nextReport = std::chrono::steady_clock::now() + 10s;
    std::optional<proto::PlayerState> pendingLocal;
    std::optional<proto::PoseState> pendingPose;
    std::uint64_t pendingSinceUs = 0, lastPoseUs = 0;
    // Once a minute (and on tools/game.sh cmd perf): the traffic, the
    // netcode's counters, the frame cost, the queues (netcode.md).
    auto nextMinute = std::chrono::steady_clock::now() + 60s;
    auto lastTraffic = endpoint->Traffic();
    auto lastTrafficUs = net::NowUs();
    auto nextStatus = std::chrono::steady_clock::now() + 500ms;
    bool ended = false;
    // Our game announced or removed NPCs of its own: the owners the scripts
    // read change with them.
    bool announced = false;
    std::uint64_t silentSinceUs = 0;
    std::uint32_t reportedRequests = 0;

    while (!stop.stop_requested() && !ended) {
      stall::NetBeat(stall::kPass);
      // Watchdog (runtime.hpp): one line when the game thread stops ticking,
      // one when it comes back. Pause menus and loading screens stop it too.
      if (const auto last = runtime.LastGameFrameUs(); last) {
        const auto now = net::NowUs();
        if (!silentSinceUs && now - last > 5'000'000) {
          silentSinceUs = last;
          log::Warn("watchdog: game thread silent for 5 s (crashed, hung, paused or loading)");
        } else if (silentSinceUs && now - last < 1'000'000) {
          log::Info("watchdog: game thread back after {:.1f} s",
                    static_cast<double>(last - silentSinceUs) / 1e6);
          silentSinceUs = 0;
        }
      }
      // 2 ms: states go out within a frame of the game handing them over.
      // `shared`: what the scripts' checks compare with changed.
      bool shared = std::exchange(announced, false);
      bool street = false;
      stall::NetBeat(stall::kUpdate);
      for (const auto &event : endpoint->Update(2ms)) {
        std::visit(
            [&](const auto &e) {
              using E = std::decay_t<decltype(e)>;
              if constexpr (std::is_same_v<E, events::Joined>) {
                log::Info("in the session as player {} (host {}{})", e.you, e.host,
                          endpoint->ThroughRelay() ? ", through the relay" : "");
                if (const auto secret = endpoint->RelaySessionSecret()) {
                  relaySecret = secret;
                  if (const auto relay = endpoint->Relay())
                    relayAt = relay->at;
                }
                wasIn = true;
                runtime.NotePlayerName(e.you, config.name);
                runtime.SetYou(e.you);
                Recording::Get().Joined(endpoint->IsHost(), e.you, e.host);
                runtime.PushEvent({ScriptEvent::Kind::InSession, e.you, {}, body});
                Publish(SessionStatus::State::InSession, {}, endpoint.get());
                // Joined: worth offering again (the host's name comes with the roster).
                if (!endpoint->IsHost() && config.joinCode.empty())
                  runtime.RememberHost({config.address, config.port}, {});
                shared = true;
                Story::Get().PushInbound(
                    {.kind = StoryInbound::Kind::InSession, .player = e.you, .host = e.host});
                Comms::Get().PushInbound({.kind = CommsInbound::Kind::InSession, .player = e.you});
              } else if constexpr (std::is_same_v<E, events::PlayerJoined>) {
                log::Info("{} joined (player {}, body {})", e.player.name, e.player.id,
                          static_cast<int>(e.player.body));
                runtime.NotePlayerName(e.player.id, e.player.name);
                if (!endpoint->IsHost() && config.joinCode.empty() &&
                    e.player.id == endpoint->HostPlayer())
                  runtime.RememberHost({config.address, config.port}, e.player.name);
                shared = true;
                runtime.PushEvent(
                    {ScriptEvent::Kind::PlayerJoined, e.player.id, e.player.name, e.player.body});
                Story::Get().PushInbound(
                    {.kind = StoryInbound::Kind::Joined, .player = e.player.id});
                Comms::Get().PushInbound(
                    {.kind = CommsInbound::Kind::Joined, .player = e.player.id});
              } else if constexpr (std::is_same_v<E, events::PlayerLeft>) {
                log::Info("player {} left", e.player);
                shared = true;
                runtime.ForgetRemote(e.player);
                runtime.PushEvent({ScriptEvent::Kind::PlayerLeft, e.player, {}, {}});
                Story::Get().PushInbound({.kind = StoryInbound::Kind::Left, .player = e.player});
                Comms::Get().PushInbound({.kind = CommsInbound::Kind::Left, .player = e.player});
              } else if constexpr (std::is_same_v<E, events::NamedAs>) {
                // Another player has our name: the session shows us as "V (2)".
                log::Info("the session shows us as {} ({} is taken)", e.shown, e.asked);
                runtime.NotePlayerName(endpoint->You(), e.shown);
                runtime.PushEvent({ScriptEvent::Kind::Renamed, endpoint->You(), e.shown, {}});
              } else if constexpr (std::is_same_v<E, events::PlayerReturned>) {
                // Its puppet and seat stay; its new game sends its look again.
                log::Info("{} is back on a new connection (player {})", e.player.name, e.player.id);
              } else if constexpr (std::is_same_v<E, events::RemoteInstall>) {
                // Another player's game against ours: told on screen, kept
                // for the menu and the report; never a reason to refuse.
                log::Info("{} (player {}) runs tandem {}, game {}, scripts {:016x}, tweaks "
                          "{:016x}, {} other mod(s) {:016x}",
                          e.name, e.player, e.install.build,
                          e.install.game.empty() ? "?" : e.install.game, e.install.scripts,
                          e.install.tweaks, e.install.modCount, e.install.mods);
                for (const auto &line : e.differences) {
                  runtime.NoteDifference(line);
                  summary.notes.push_back(line);
                  Notice(line, line, 600'000'000);
                }
              } else if constexpr (std::is_same_v<E, events::RemoteMods>) {
                ModRecords::Get().OnCompared(e);
                if (e.comparison.Differ())
                  summary.notes.push_back(mods::Describe(e.comparison, e.name));
              } else if constexpr (std::is_same_v<E, events::ModNamesFrom>) {
                ModRecords::Get().OnNames(e);
              } else if constexpr (std::is_same_v<E, events::SubstitutesFrom>) {
                ModRecords::Get().OnSeen(e);
              } else if constexpr (std::is_same_v<E, events::JoinRefused>) {
                runtime.NoteDifference(e.notice);
                summary.notes.push_back(e.notice);
                Notice("refused " + e.name, e.notice);
              } else if constexpr (std::is_same_v<E, events::RemoteState>) {
                runtime.PushRemote(e.state);
              } else if constexpr (std::is_same_v<E, events::RemotePose>) {
                runtime.PushRemotePose(e.pose);
              } else if constexpr (std::is_same_v<E, events::HostActorPose>) {
                runtime.PushActorPose(e.pose);
              } else if constexpr (std::is_same_v<E, events::RemoteHeld>) {
                runtime.PushRemoteHeld(e.held);
              } else if constexpr (std::is_same_v<E, events::RemoteShot>) {
                runtime.PushRemoteShot(e.shot);
              } else if constexpr (std::is_same_v<E, events::RemoteSeat>) {
                log::Info("player {}: seat {:#x} in vehicle {:#x} of player {}", e.seat.player,
                          e.seat.seat, e.seat.vehicle, e.seat.owner);
                runtime.PushRemoteSeat(e.seat);
              } else if constexpr (std::is_same_v<E, events::RemoteVehicle>) {
                runtime.PushRemoteVehicle(e.vehicle);
              } else if constexpr (std::is_same_v<E, events::RemoteTransit>) {
                log::Info("player {}: ride {} (flags {:#x}, station {} next {}, seat {:#x})",
                          e.transit.player, static_cast<int>(e.transit.kind), e.transit.flags,
                          e.transit.at, e.transit.next, e.transit.seat);
                runtime.PushRemoteTransit(e.transit);
              } else if constexpr (std::is_same_v<E, events::HostNpcSeat>) {
                runtime.PushNpcEvent({.kind = NpcEvent::Kind::Seat,
                                      .spawn = {.npc = e.seat.npc, .appearance = e.seat.seat},
                                      .player = e.seat.owner});
              } else if constexpr (std::is_same_v<E, events::RemoteVehicleRadio>) {
                log::Info("player {}: car radio {}, station {}, track {:#x}{}", e.radio.player,
                          e.radio.on ? "on" : "off", e.radio.station, e.radio.track,
                          e.radio.trackStartUs ? " (start known)" : "");
                runtime.PushRemoteRadio(e.radio);
              } else if constexpr (std::is_same_v<E, events::HostWorld>) {
                runtime.PushHostWorld(e.world);
              } else if constexpr (std::is_same_v<E, events::HostNpcSpawn>) {
                shared = true;
                runtime.PushNpcEvent({NpcEvent::Kind::Spawn, e.spawn, 0});
              } else if constexpr (std::is_same_v<E, events::HostNpcGone>) {
                shared = true;
                runtime.PushNpcEvent({.kind = NpcEvent::Kind::Gone,
                                      .spawn = {.npc = e.gone.npc},
                                      .reason = e.gone.reason,
                                      .player = e.gone.killer});
                runtime.ForgetRemote(kNpcPuppetBase + e.gone.npc);
              } else if constexpr (std::is_same_v<E, events::HostNpcHealth>) {
                runtime.PushNpcEvent({.kind = NpcEvent::Kind::Health,
                                      .spawn = {.npc = e.health.npc},
                                      .amount = e.health.health,
                                      .flags = e.health.flags});
              } else if constexpr (std::is_same_v<E, events::NpcEffectBy>) {
                for (const auto effect : e.effect.effects)
                  runtime.PushNpcEvent({.kind = NpcEvent::Kind::Effect,
                                        .spawn = {.npc = e.effect.npc, .record = effect},
                                        .player = e.effect.player});
              } else if constexpr (std::is_same_v<E, events::NpcHitBy>) {
                // An older guest's hit: applied as a strike (Outcomes.reds).
                Combat::Get().PushInbound(combat::FromHit(e.hit));
              } else if constexpr (std::is_same_v<E, events::Combat>) {
                // A street race's word: Races.reds (activity.hpp), not the hits'.
                if (const auto *race = std::get_if<proto::Race>(&e.message))
                  Activities::Get().PushRace({.kind = RaceNote::Kind::Heard, .race = *race});
                else
                  Combat::Get().PushInbound(e.message);
              } else if constexpr (std::is_same_v<E, events::Trade>) {
                TradeLink::Get().PushInbound(e.message);
              } else if constexpr (std::is_same_v<E, events::NpcActionBy>) {
                const auto &a = e.action;
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Request,
                                         .npc = a.npc,
                                         .player = a.player,
                                         .action = static_cast<std::uint8_t>(a.kind),
                                         .name = a.name,
                                         .item = a.item,
                                         .amount = a.amount,
                                         .direction = a.direction});
              } else if constexpr (std::is_same_v<E, events::NpcActionAnswered>) {
                const auto &r = e.reply;
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Reply,
                                         .npc = r.npc,
                                         .action = static_cast<std::uint8_t>(r.kind),
                                         .accepted = r.accepted,
                                         .reason = r.reason});
              } else if constexpr (std::is_same_v<E, events::HostNpcHold>) {
                const auto &h = e.hold;
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Hold,
                                         .npc = h.npc,
                                         .player = h.player,
                                         .action = static_cast<std::uint8_t>(h.how),
                                         .name = h.name});
              } else if constexpr (std::is_same_v<E, events::HostNpcCue>) {
                runtime.PushActionEvent({.kind = ActionEvent::Kind::Cue,
                                         .npc = e.cue.npc,
                                         .player = e.cue.player,
                                         .action = static_cast<std::uint8_t>(e.cue.cue)});
              } else if constexpr (std::is_same_v<E, events::NpcHackBy>) {
                runtime.PushHackEvent({.kind = HackEvent::Kind::Upload,
                                       .npc = e.hack.npc,
                                       .player = e.hack.player,
                                       .action = e.hack.action,
                                       .flags = e.hack.flags,
                                       .seconds = static_cast<float>(e.hack.uploadMs) / 1000.f});
              } else if constexpr (std::is_same_v<E, events::HostNpcHackState>) {
                runtime.PushHackEvent({.kind = HackEvent::Kind::State,
                                       .npc = e.state.npc,
                                       .player = e.state.player,
                                       .action = e.state.action,
                                       .state = static_cast<std::int32_t>(e.state.step),
                                       .seconds = static_cast<float>(e.state.uploadMs) / 1000.f});
              } else if constexpr (std::is_same_v<E, events::HostNpcStatus>) {
                runtime.PushHackEvent({.kind = HackEvent::Kind::Status,
                                       .npc = e.status.npc,
                                       .player = e.status.player,
                                       .action = e.status.effect,
                                       .state = e.status.applied ? 1 : 0});
              } else if constexpr (std::is_same_v<E, events::HostPlayerHack>) {
                log::Info("npc {} hacks our V ({:#x})", e.hack.npc, e.hack.action);
                runtime.PushHackEvent({.kind = HackEvent::Kind::PlayerHack,
                                       .npc = e.hack.npc,
                                       .player = e.hack.player,
                                       .action = e.hack.action,
                                       .flags = e.hack.flags});
              } else if constexpr (std::is_same_v<E, events::VehicleHackOn>) {
                log::Info("player {} hacked the car we drive ({})", e.hack.player,
                          static_cast<int>(e.hack.hack));
                runtime.PushHackEvent({.kind = HackEvent::Kind::Car,
                                       .player = e.hack.player,
                                       .state = static_cast<std::int32_t>(e.hack.hack),
                                       .seconds = e.hack.seconds});
              } else if constexpr (std::is_same_v<E, events::TrafficHacked>) {
                log::Info("player {} hacked vehicle {:#x} ({}, {:.1f} s{})", e.hack.player,
                          e.hack.id, static_cast<int>(e.hack.hack), e.hack.seconds,
                          e.ours ? ", ours to apply" : "");
                runtime.PushHackEvent({.kind = HackEvent::Kind::Traffic,
                                       .npc = e.hack.id,
                                       .player = e.hack.player,
                                       .state = static_cast<std::int32_t>(e.hack.hack),
                                       .flags = e.ours ? 1 : 0,
                                       .seconds = e.hack.seconds});
              } else if constexpr (std::is_same_v<E, events::HitByNpc>) {
                runtime.PushPlayerHit(e.hit);
              } else if constexpr (std::is_same_v<E, events::ShotAtUs>) {
                runtime.PushShotAt(e.shot);
              } else if constexpr (std::is_same_v<E, events::ShotHitBy>) {
                runtime.PushShotHit(e.hit);
              } else if constexpr (std::is_same_v<E, events::TrafficSpawned>) {
                street = true;
                runtime.PushStreetEvent(
                    {.kind = StreetEvent::Kind::Spawn, .spawn = e.spawn, .flags = e.spawn.flags});
              } else if constexpr (std::is_same_v<E, events::TrafficMoved>) {
                runtime.PushTrafficStates(e.states);
              } else if constexpr (std::is_same_v<E, events::TrafficLeft>) {
                street = true;
                if (e.gone.reason == proto::kTrafficTaken)
                  log::Info("street: vehicle {:#x} is player {}'s car now", e.gone.id,
                            e.gone.player);
                runtime.PushStreetEvent({.kind = StreetEvent::Kind::Gone,
                                         .spawn = {.id = e.gone.id},
                                         .reason = e.gone.reason,
                                         .player = e.gone.player});
                runtime.ForgetTraffic(e.gone.id);
              } else if constexpr (std::is_same_v<E, events::TrafficChanged>) {
                street = true;
                runtime.PushStreetEvent({.kind = StreetEvent::Kind::Status,
                                         .spawn = {.id = e.status.id},
                                         .flags = e.status.flags,
                                         .amount = e.status.health});
              } else if constexpr (std::is_same_v<E, events::TrafficGive>) {
                street = true;
                log::Info("street: the host asks us to give vehicle {:#x} to player {}", e.id,
                          e.to);
                runtime.PushStreetEvent(
                    {.kind = StreetEvent::Kind::Give, .spawn = {.id = e.id}, .player = e.to});
              } else if constexpr (std::is_same_v<E, events::TrafficTake>) {
                street = true;
                const auto &s = e.state;
                log::Info("street: vehicle {:#x} is ours now (from player {}{})", s.id, s.from,
                          (s.handover & proto::kHandoverInherited) ? ", inherited" : "");
                // Ours to move now: the giver's track is of no use (a new one
                // starts if it is ever another game's again).
                runtime.ForgetTraffic(s.id);
                runtime.PushStreetEvent({.kind = StreetEvent::Kind::Take,
                                         .spawn = {.id = s.id,
                                                   .position = s.position,
                                                   .orientation = s.orientation,
                                                   .flags = s.flags},
                                         .flags = s.flags,
                                         .reason = s.handover,
                                         .player = s.from,
                                         .amount = s.health,
                                         .vector = s.velocity});
              } else if constexpr (std::is_same_v<E, events::TrafficOwnerChanged>) {
                street = true;
                log::Info("street: vehicle {:#x} is run by player {} now (was {})", e.id, e.owner,
                          e.was);
                runtime.PushStreetEvent({.kind = StreetEvent::Kind::Owner,
                                         .spawn = {.id = e.id},
                                         .reason = static_cast<std::int32_t>(e.was),
                                         .player = e.owner});
              } else if constexpr (std::is_same_v<E, events::TrafficHitBy>) {
                runtime.PushStreetEvent({.kind = StreetEvent::Kind::Hit,
                                         .spawn = {.id = e.hit.id, .position = e.hit.position},
                                         .flags = e.hit.flags,
                                         .player = e.hit.player,
                                         .amount = e.hit.damage,
                                         .vector = e.hit.impulse});
              } else if constexpr (std::is_same_v<E, events::RemoteHeat>) {
                log::Info("player {}: wanted level {} (state {})", e.heat.player, e.heat.stage,
                          static_cast<int>(e.heat.state));
                runtime.PushRemoteHeat(e.heat);
              } else if constexpr (std::is_same_v<E, events::RemoteVehicleDamage>) {
                const auto &d = e.damage;
                log::Info("wheels: vehicle {:#x} of player {}: health {:.2f}, flags {:#x}, tyres "
                          "{:#x}, {} part(s) off",
                          d.vehicle, endpoint->VehicleOwner(d.vehicle), d.health, d.flags, d.tyres,
                          d.parts.size());
                runtime.PushVehicleDamage(d);
              } else if constexpr (std::is_same_v<E, events::VehicleHitBy>) {
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Hit,
                                         .player = e.hit.player,
                                         .vehicle = e.hit.vehicle,
                                         .a = e.hit.flags,
                                         .b = e.hit.tyre,
                                         .c = e.hit.window,
                                         .amount = e.hit.damage,
                                         .position = e.hit.position,
                                         .vector = e.hit.direction});
              } else if constexpr (std::is_same_v<E, events::RemoteVehicleExit>) {
                log::Info("wheels: player {} left vehicle {:#x} the hard way (kind {}, flags {})",
                          e.exit.player, e.exit.vehicle, static_cast<int>(e.exit.kind),
                          e.exit.flags);
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Exit,
                                         .player = e.exit.player,
                                         .vehicle = e.exit.vehicle,
                                         .a = static_cast<std::int32_t>(e.exit.kind),
                                         .b = e.exit.flags,
                                         .vector = e.exit.velocity});
              } else if constexpr (std::is_same_v<E, events::RemoteVehicleCombat>) {
                runtime.SetSeatCombat(e.combat);
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Combat,
                                         .player = e.combat.player,
                                         .vehicle = e.combat.vehicle,
                                         .a = static_cast<std::int32_t>(e.combat.mode),
                                         .id = e.combat.weapon});
              } else if constexpr (std::is_same_v<E, events::RemoteMountedFire>) {
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Fire,
                                         .player = e.fire.player,
                                         .vehicle = e.fire.vehicle,
                                         .id = e.fire.weapon,
                                         .id2 = e.fire.slot,
                                         .position = e.fire.target});
              } else if constexpr (std::is_same_v<E, events::GunnerChanged>) {
                log::Info("wheels: vehicle {:#x}: gunner {}", e.gunner.vehicle, e.gunner.gunner);
                runtime.SetGunner(e.gunner);
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Gunner,
                                         .player = e.gunner.gunner,
                                         .vehicle = e.gunner.vehicle,
                                         .id = e.gunner.seat});
              } else if constexpr (std::is_same_v<E, events::RemoteGunnerAim>) {
                runtime.PushGunnerAim(e.aim);
              } else if constexpr (std::is_same_v<E, events::VehicleBumpBy>) {
                runtime.PushBump(e.bump);
              } else if constexpr (std::is_same_v<E, events::NpcRamBy>) {
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Ram,
                                         .player = e.ram.player,
                                         .a = e.ram.flags,
                                         .npc = e.ram.npc,
                                         .position = e.ram.position,
                                         .vector = e.ram.velocity});
              } else if constexpr (std::is_same_v<E, events::ChaseChanged>) {
                log::Info("wheels: vehicle {:#x} chases player {}", e.chase.vehicle,
                          e.chase.target);
                runtime.PushWheelsEvent({.kind = WheelsEvent::Kind::Chase,
                                         .player = e.chase.target,
                                         .vehicle = e.chase.vehicle,
                                         .a = e.chase.flags});
              } else if constexpr (std::is_same_v<E, events::CrimeReported>) {
                log::Info("police: our V's crime seen by the area's owner (kind {}, npc {})",
                          static_cast<int>(e.crime.kind), e.crime.npc);
                runtime.PushStreetEvent(
                    {.kind = StreetEvent::Kind::Crime,
                     .spawn = {.id = e.crime.traffic, .position = e.crime.position},
                     .flags = e.crime.flags,
                     .reason = static_cast<std::int32_t>(e.crime.kind),
                     .amount = e.crime.amount,
                     .npc = e.crime.npc,
                     .attack = e.crime.attackType});
              } else if constexpr (std::is_same_v<E, events::HostNpcStates>) {
                runtime.PushNpcStates(e.states, endpoint->You());
              } else if constexpr (std::is_same_v<E, events::HostNpcAwareness>) {
                runtime.PushNpcAwareness(e.awareness, endpoint->You());
              } else if constexpr (std::is_same_v<E, events::RemoteStealth>) {
                log::Info("player {}: stealth flags {:#x}, visibility {:.2f}, camo {:#x}",
                          e.stealth.player, e.stealth.flags, e.stealth.visibility, e.stealth.camo);
                runtime.PushRemoteStealth(e.stealth);
              } else if constexpr (std::is_same_v<E, events::NoiseBy>) {
                runtime.PushRemoteNoise(e.noise);
              } else if constexpr (std::is_same_v<E, events::HostNpcAnims>) {
                runtime.PushNpcAnims(e.anims);
              } else if constexpr (std::is_same_v<E, events::HostNpcShot>) {
                runtime.PushNpcShot(e.shot);
              } else if constexpr (std::is_same_v<E, events::HostNpcReaction>) {
                PresenceState::Get().PushReaction(e.reaction);
              } else if constexpr (std::is_same_v<E, events::RemotePresence>) {
                PresenceState::Get().PushRemote(e.presence);
              } else if constexpr (std::is_same_v<E, events::RemoteCue>) {
                PresenceState::Get().PushCue(e.cue, net::NowUs());
              } else if constexpr (std::is_same_v<E, events::RemoteStatusVisuals>) {
                log::Info("player {}: {} status effect(s) show on its V (revision {})",
                          e.visuals.player, e.visuals.records.size(), e.visuals.revision);
                PresenceState::Get().PushStatusVisuals(e.visuals);
              } else if constexpr (std::is_same_v<E, events::RemoteAppearance>) {
                log::Info("player {}: look of {} keys and {} items (revision {})",
                          e.appearance.player, e.appearance.look.size(), e.appearance.items.size(),
                          e.appearance.revision);
                runtime.PushRemoteAppearance(e.appearance);
              } else if constexpr (std::is_same_v<E, events::PauseChanged>) {
                log::Info("player {} {} (pause menu)", e.pause.player,
                          e.pause.paused ? "paused" : "resumed");
                PublishPaused(*endpoint);
              } else if constexpr (std::is_same_v<E, events::RegroupOffered>) {
                log::Info("player {} offers to join them (offer {}, reason {}, point {:#x}, at "
                          "({:.1f}, {:.1f}, {:.1f}))",
                          e.offer.from, e.offer.offer, static_cast<int>(e.offer.reason),
                          e.offer.point, e.offer.position.x, e.offer.position.y,
                          e.offer.position.z);
                runtime.PushRegroupEvent({.kind = RegroupEvent::Kind::Offer, .offer = e.offer});
              } else if constexpr (std::is_same_v<E, events::RegroupAnswered>) {
                log::Info("player {} {} our offer {}", e.answer.from,
                          e.answer.accepted ? "accepted" : "declined", e.answer.offer);
                runtime.PushRegroupEvent({.kind = RegroupEvent::Kind::Answer, .answer = e.answer});
              } else if constexpr (std::is_same_v<E, events::RemoteVitals>) {
                log::Info("player {}: life {} (flags {:#x})", e.vitals.player,
                          static_cast<int>(e.vitals.life), e.vitals.flags);
              } else if constexpr (std::is_same_v<E, events::ReviveBy>) {
                log::Info("player {} revives our V: step {}", e.revive.player,
                          static_cast<int>(e.revive.step));
                runtime.PushRevive(e.revive);
              } else if constexpr (std::is_same_v<E, events::SessionLost>) {
                log::Info("session lost: no V is up (loss {})", e.over.round);
                runtime.NoteSessionLost();
              } else if constexpr (std::is_same_v<E, events::Story>) {
                // The host's camera is a stream: straight to its track, not
                // through the script's queue.
                if (const auto *camera = std::get_if<proto::SceneCamera>(&e.message))
                  Story::Get().PushCamera(*camera);
                else if (const auto *face = std::get_if<proto::RigPose>(&e.message))
                  runtime.PushRigPose(*face);
                else if (const auto *tracks = std::get_if<proto::RigTracks>(&e.message))
                  runtime.PushRigTracks(*tracks);
                else
                  Story::Get().PushInbound(
                      {.kind = StoryInbound::Kind::Message, .message = e.message});
              } else if constexpr (std::is_same_v<E, events::RemoteTimeEffect>) {
                log::Info("player {}: time abilities {:#x} (world speed {:.2f})", e.effect.player,
                          e.effect.abilities, e.effect.scale);
                runtime.PushRemoteTime(e.effect);
              } else if constexpr (std::is_same_v<E, events::RemoteElsewhere>) {
                log::Info("player {}: place {} (flags {:#x}, body at {:.1f} {:.1f} {:.1f})",
                          e.state.player, static_cast<int>(e.state.place), e.state.flags,
                          e.state.anchor.x, e.state.anchor.y, e.state.anchor.z);
                runtime.PushRemoteElsewhere(e.state);
              } else if constexpr (std::is_same_v<E, events::RemoteActivity>) {
                log::Info("player {}: busy {} (flags {:#x}, workspot {:#x} entry {}, device {:#x})",
                          e.state.player, static_cast<int>(e.state.kind), e.state.flags,
                          e.state.workspot, e.state.entry, e.state.device);
                Activities::Get().PushRemote(e.state);
              } else if constexpr (std::is_same_v<E, events::TimeSkipAsked>) {
                log::Info("player {} asks to skip time to {} (request {}, reason {})", e.skip.from,
                          e.skip.toSeconds, e.skip.request, static_cast<int>(e.skip.reason));
                Activities::Get().PushEvent({.kind = SkipEvent::Kind::Asked, .skip = e.skip});
              } else if constexpr (std::is_same_v<E, events::TimeSkipVoted>) {
                log::Info("player {} {} our time skip {}", e.vote.from,
                          e.vote.accepted ? "accepted" : "declined", e.vote.request);
                Activities::Get().PushEvent({.kind = SkipEvent::Kind::Voted, .vote = e.vote});
              } else if constexpr (std::is_same_v<E, events::TimeSkipEnded>) {
                log::Info("time skip {} of player {}: outcome {} (by {}, why {}), {} -> {}",
                          e.result.request, e.result.asker, static_cast<int>(e.result.outcome),
                          e.result.by, e.result.why, e.result.fromSeconds, e.result.toSeconds);
                Activities::Get().PushEvent({.kind = SkipEvent::Kind::Ended, .result = e.result});
              } else if constexpr (std::is_same_v<E, events::WorldItemAppeared>) {
                log::Info("loot: world item {} dropped by player {} ({} stacks, token {})",
                          e.item.id, e.item.dropper, e.item.stacks.size(), e.item.token);
                runtime.PushLoot(e.item);
              } else if constexpr (std::is_same_v<E, events::WorldItemTakenBy>) {
                log::Info("loot: world item {} stack {} taken by player {}", e.taken.id,
                          e.taken.stack, e.taken.taker);
                runtime.PushLoot(e.taken);
              } else if constexpr (std::is_same_v<E, events::SharedItemFrom>) {
                log::Info("loot: player {} shares item {:#x} x{} ({}, npc {})", e.item.player,
                          e.item.item.record, e.item.item.quantity,
                          e.item.kind == proto::kSharedOnCorpse ? "on a corpse" : "looted",
                          e.item.npc);
                runtime.PushLoot(e.item);
              } else if constexpr (std::is_same_v<E, events::HostDevice>) {
                runtime.PushDevice(e.state);
              } else if constexpr (std::is_same_v<E, events::DeviceAsked>) {
                auto asked = e.state;
                asked.how = proto::kDeviceAsk;
                runtime.PushDevice(asked);
              } else if constexpr (std::is_same_v<E, events::DeviceRequested>) {
                log::Info("devices: player {} operated {:x} ({:#x}): status {}, flags {:#x}, "
                          "value {}",
                          e.use.player, e.use.entity, e.use.cls, static_cast<int>(e.use.status),
                          e.use.flags, e.use.value);
                runtime.PushDevice(e.use);
              } else if constexpr (std::is_same_v<E, events::BreachBy>) {
                log::Info("breach: player {}'s of network {:x} ({:#x}), npc {}: {} daemon(s), "
                          "{} trap(s), flags {:#x}: ours to apply",
                          e.breach.player, e.breach.entity, e.breach.cls, e.breach.npc,
                          e.breach.programs.size(), e.breach.traps.size(), e.breach.flags);
                runtime.PushBreach(e.breach);
              } else if constexpr (std::is_same_v<E, events::BreachAnswered>) {
                log::Info("breach: the host's answer on network {:x}, npc {}: verdict {}, by "
                          "player {}",
                          e.result.entity, e.result.npc, static_cast<int>(e.result.verdict),
                          e.result.by);
                runtime.PushBreach(e.result);
              } else if constexpr (std::is_same_v<E, events::Fractured>) {
                runtime.PushObject(e.fracture);
              } else if constexpr (std::is_same_v<E, events::DeviceFired>) {
                runtime.PushObject(e.fire);
              } else if constexpr (std::is_same_v<E, events::TurretHeld>) {
                runtime.PushObject(e.hands);
              } else if constexpr (std::is_same_v<E, events::TurretRefused>) {
                log::Info("turrets: our claim on {:x} refused: player {} holds it", e.busy.entity,
                          e.busy.holder);
                runtime.PushObject(e.busy);
              } else if constexpr (std::is_same_v<E, events::DevicesReset>) {
                log::Info("devices: the host sends its record again");
                runtime.PushDevice(proto::DeviceSet{});
              } else if constexpr (std::is_same_v<E, events::Resync>) {
                if (e.player == endpoint->You()) {
                  log::Info("resync: asking the host for {} (our copy differed from its digest)",
                            Domains(e.domains));
                  // The world alone heals silently (a clock, a weather).
                  if (e.domains &
                      (proto::kDomainNpcs | proto::kDomainStory | proto::kDomainPlayers))
                    Notice("resync", "Resyncing with the host...");
                } else {
                  log::Info("resync: player {} asked for {}", e.player, Domains(e.domains));
                }
              } else if constexpr (std::is_same_v<E, events::EpochChanged>) {
                log::Info("epoch: player {}'s world was rebuilt ({}, epoch {})", e.epoch.player,
                          EpochReason(e.epoch.reason), e.epoch.epoch);
              } else if constexpr (std::is_same_v<E, events::RegionsChanged>) {
                std::string who;
                for (const auto player : e.linked)
                  who += (who.empty() ? "" : ", ") + std::to_string(player);
                log::Info("regions: linked with {} (leader: player {})",
                          who.empty() ? std::string("nobody: our world is our own")
                                      : "player " + who,
                          e.leader);
                shared = true;
                Regions::Get().SetLinked(e.linked);
                Regions::Get().Push({.kind = RegionEvent::Kind::Linked});
              } else if constexpr (std::is_same_v<E, events::NpcGive>) {
                Regions::Get().Push(
                    {.kind = RegionEvent::Kind::Give, .npc = e.npc, .player = e.to});
              } else if constexpr (std::is_same_v<E, events::NpcTake>) {
                log::Info("regions: npc {} is ours to run, from player {} ({})", e.state.npc,
                          e.state.from,
                          e.state.flags & proto::kHandoverInherited ? "inherited" : "handed over");
                shared = true;
                // Its track ends here: our game moves it from now on.
                runtime.ForgetRemote(kNpcPuppetBase + e.state.npc);
                Regions::Get().Push({.kind = RegionEvent::Kind::Take,
                                     .npc = e.state.npc,
                                     .player = e.state.from,
                                     .state = e.state});
              } else if constexpr (std::is_same_v<E, events::NpcOwnerChanged>) {
                log::Info("regions: npc {} is now run by player {} (was {})", e.npc, e.owner,
                          e.was);
                shared = true;
                Regions::Get().Push({.kind = RegionEvent::Kind::Owner,
                                     .npc = e.npc,
                                     .player = e.owner,
                                     .was = e.was});
              } else if constexpr (std::is_same_v<E, events::MarkFrom>) {
                log::Info("comms: player {} pinged (mark {}, kind {}, target {}:{:#x}, at ({:.1f}, "
                          "{:.1f}, {:.1f}))",
                          e.mark.from, e.mark.id, static_cast<int>(e.mark.kind),
                          static_cast<int>(e.mark.target.kind), e.mark.target.id, e.mark.position.x,
                          e.mark.position.y, e.mark.position.z);
                Comms::Get().PushInbound({.kind = CommsInbound::Kind::Message, .message = e.mark});
              } else if constexpr (std::is_same_v<E, events::TagFrom>) {
                log::Info("comms: player {} {} {} target(s) ({})", e.tag.from,
                          e.tag.on ? "tagged" : "untagged", e.tag.targets.size(),
                          e.tag.reason == proto::TagReason::Reveal ? "reveal" : "scan");
                Comms::Get().PushInbound({.kind = CommsInbound::Kind::Message, .message = e.tag});
              } else if constexpr (std::is_same_v<E, events::RemoteLink>) {
                log::Info("comms: player {}'s link: mode {}, {} listed", e.link.player,
                          static_cast<int>(e.link.mode), e.link.players.size());
                Comms::Get().PushInbound({.kind = CommsInbound::Kind::Message, .message = e.link});
              } else if constexpr (std::is_same_v<E, events::RemoteWaypoint>) {
                log::Info("comms: player {}'s waypoint: kind {} at ({:.1f}, {:.1f}, {:.1f})",
                          e.waypoint.player, static_cast<int>(e.waypoint.kind),
                          e.waypoint.position.x, e.waypoint.position.y, e.waypoint.position.z);
                Comms::Get().PushInbound(
                    {.kind = CommsInbound::Kind::Message, .message = e.waypoint});
              } else if constexpr (std::is_same_v<E, events::LinkDeclined>) {
                log::Info("comms: player {} did not take our link request (reason {})",
                          e.decline.from, e.decline.reason);
                Comms::Get().PushInbound(
                    {.kind = CommsInbound::Kind::Message, .message = e.decline});
              } else if constexpr (std::is_same_v<E, events::Ended>) {
                Regions::Get().Reset();
                Story::Get().PushInbound({.kind = StoryInbound::Kind::Ended});
                Comms::Get().PushInbound({.kind = CommsInbound::Kind::Ended});
                log::Info("session ended: {}", e.reason);
                runtime.SetYou(0);
                runtime.SetRemotePaused({});
                runtime.SetOthersLives({});
                // Nobody of this session is anywhere for us any more.
                runtime.ClearRemoteElsewhere();
                Activities::Get().EndSession();
                // The session's vendors, lends and rules (an NPC id or a
                // player number the next one reuses is someone else's).
                Combat::Get().EndSession();
                // Our seat and ride are said again to the next session
                // only when they name nobody of this one.
                if (lastSeat && !consistency::CarriesOver(*lastSeat))
                  lastSeat.reset();
                if (lastTransit && !consistency::CarriesOver(*lastTransit))
                  lastTransit.reset();
                // The next session's players start afresh (a restarted host
                // counts its states from the start again).
                for (const auto &[id, info] : endpoint->Players())
                  if (id != endpoint->You())
                    runtime.ForgetRemote(id);
                runtime.PushEvent({ScriptEvent::Kind::SessionEnded, 0, e.reason, {}});
                // Refused for our version or for Phantom Liberty: said on
                // screen (the session's end is only said to a player who
                // was in it).
                if (compat::IsFinalRefusal(e.reason)) {
                  const std::string_view words = e.reason;
                  const auto text =
                      "Could not join: " +
                      std::string(words.substr(words.starts_with("refused: ") ? 9 : 0));
                  refusal = text;
                  summary.notes.push_back(text);
                  Notice("version", text);
                }
                endReason = e.reason;
                runtime.ResetSession();
                ended = true;
              }
            },
            event);
      }
      stall::NetBeat(stall::kTake);
      const Perf::Scope pass(Work::NetPass);
      if (street && !ended)
        ShareStreet(runtime, *endpoint);
      summary.peakPlayers = std::max(summary.peakPlayers, endpoint->Players().size());
      if (const auto lost = runtime.TakeOverflows())
        log::Warn("consistency: {} event(s) dropped, a queue was full (the game was not taking "
                  "them); its checks put the proxies right",
                  lost);
      // Our world was rebuilt (a fast travel, Consistency.reds).
      if (const auto reason = runtime.TakeEpochRequest(); reason && !ended) {
        endpoint->BumpEpoch(*reason);
        log::Info("epoch: our world was rebuilt ({}), epoch {}", EpochReason(*reason),
                  endpoint->WorldEpoch());
      }

      if (auto status = endpoint->Relay()) {
        status->codeSecondsLeft = 0; // not a change worth a line
        if (status != relayShown) {
          if (!status->code.empty() && (!relayShown || relayShown->code != status->code))
            log::Info("join code {} (relay {}:{}, replica {})", net::DisplayJoinCode(status->code),
                      config.relay, config.relayPort, status->at);
          if (!status->error.empty() && (!relayShown || relayShown->error != status->error))
            log::Warn("relay: {}", status->error);
          else if (status->state == net::RelayStatus::State::Ready &&
                   (!relayShown || relayShown->state != status->state))
            log::Info("relay: registered");
          if (status->direct != (relayShown ? relayShown->direct : 0))
            log::Info("relay: {} direct path(s) up", status->direct);
          if (status->nat != (relayShown ? relayShown->nat : net::NatMapping::Unknown))
            log::Info("relay: connection check: public {}{}, {}", status->publicAddress,
                      status->portKept ? " (our own port)" : "",
                      status->nat == net::NatMapping::Stable ? "direct connections likely"
                                                             : "relay only (symmetric NAT)");
          runtime.SetRelay(status->code.empty() ? "" : net::DisplayJoinCode(status->code),
                           StatusText(*status));
          runtime.SetRelayDetails({.kept = status->codeKept,
                                   .check = std::string(net::DirectOutlook(*status)),
                                   .direct = static_cast<std::int32_t>(status->direct)});
          relayShown = status;
        }
      }
      // The settings page's `direct`, and the co-op menu's New join code.
      if (const bool direct = runtime.directOn; direct != directApplied) {
        endpoint->SetDirect(direct);
        directApplied = direct;
      }
      if (const auto asked = runtime.newCodeRequests.load(); asked != newCodesAsked) {
        newCodesAsked = asked;
        endpoint->NewRelayCode();
      }

      runtime.SetClockOffset(static_cast<std::int64_t>(endpoint->HostNowUs()) -
                             static_cast<std::int64_t>(net::NowUs()));
      if (auto request = runtime.TakeRecordingRequest())
        Record(*request, *endpoint);
      // Our V's state (the script tick) and pose (the animation job) come a
      // few ms apart: the first waits up to kCoalesceUs for the other, so
      // both go in one datagram (netcode.md: 32 bytes of headers saved 30
      // times a second). Without a pose stream the state goes at once.
      if (auto local = runtime.TakeLocal()) {
        Story::Get().FilterLocal(*local); // a private scene: held where it began
        if (!pendingLocal)
          pendingSinceUs = net::NowUs();
        pendingLocal = local;
      }
      // A private scene, or a game standing still (photo mode, a menu that
      // stops it): no poses, so the others' puppet idles on its own graph.
      if (auto pose = runtime.TakeLocalPose();
          pose && !Story::Get().Private() && !Activities::Get().LocalFrozen()) {
        if (!pendingPose && !pendingLocal)
          pendingSinceUs = net::NowUs();
        pendingPose = pose;
        lastPoseUs = net::NowUs();
      }
      if (pendingLocal || pendingPose) {
        const auto nowUs = net::NowUs();
        const bool poseLive = lastPoseUs && nowUs - lastPoseUs < 200'000;
        if ((pendingLocal && pendingPose) || (pendingLocal && !poseLive) ||
            nowUs - pendingSinceUs >= kCoalesceUs) {
          if (pendingLocal)
            endpoint->SendLocalState(*std::exchange(pendingLocal, std::nullopt));
          if (pendingPose)
            endpoint->SendLocalPose(*std::exchange(pendingPose, std::nullopt));
        }
      }
      for (const auto &pose : runtime.TakeActorPoses())
        if (!Story::Get().Private())
          endpoint->SendActorPose(pose);
      for (const auto &sample : runtime.TakeRigPoses())
        if (!Story::Get().Private() && sample.reference) {
          endpoint->SendRigPose(sample.npc, sample.part, sample.hash, *sample.reference,
                                sample.pose);
          if (sample.trackReference && !sample.tracks.empty())
            endpoint->SendRigTracks(sample.npc, sample.part, sample.trackHash,
                                    *sample.trackReference, sample.tracks);
        }
      if (const auto camera = Story::Get().TakeCamera(); camera && !Story::Get().Private())
        endpoint->SendSceneCamera(*camera);
      if (auto appearance = runtime.TakeLocalAppearance()) {
        lastAppearance = *appearance;
        endpoint->SetLocalAppearance(std::move(*appearance));
      }
      if (auto held = runtime.TakeLocalHeld()) {
        lastHeld = *held;
        endpoint->SetLocalHeld(*held);
      }
      for (const auto &shot : runtime.TakeLocalShots())
        endpoint->SendLocalShot(shot);
      if (auto seat = runtime.TakeLocalSeat()) {
        lastSeat = *seat;
        endpoint->SetLocalSeat(*seat);
      }
      if (auto vehicle = runtime.TakeLocalVehicle())
        endpoint->SendLocalVehicle(*vehicle);
      if (auto transit = runtime.TakeLocalTransit()) {
        lastTransit = *transit;
        endpoint->SetLocalTransit(*transit);
        log::Info("our ride {} (flags {:#x}, station {} next {})", static_cast<int>(transit->kind),
                  transit->flags, transit->at, transit->next);
      }
      for (const auto &radio : runtime.TakeLocalRadios()) {
        lastRadio = radio;
        endpoint->SetLocalVehicleRadio(radio);
      }
      if (auto paused = runtime.TakeLocalPaused()) {
        lastPaused = *paused;
        endpoint->SetLocalPaused(*paused);
        log::Info("our pause menu {}", *paused ? "opened: pausing everyone" : "closed");
      }
      if (auto time = runtime.TakeLocalTime()) {
        lastTime = *time;
        endpoint->SetLocalTimeEffect(*time);
        log::Info("our time abilities {:#x} (world speed {:.2f})", time->abilities, time->scale);
      }
      if (auto away = runtime.TakeLocalElsewhere()) {
        lastElsewhere = *away;
        endpoint->SetLocalElsewhere(*away);
        log::Info("our place {} (flags {:#x})", static_cast<int>(away->place), away->flags);
      }
      if (auto busy = Activities::Get().TakeLocal()) {
        lastActivity = *busy;
        endpoint->SetLocalActivity(*busy);
        log::Info("our V busy {} (flags {:#x}, workspot {:#x})", static_cast<int>(busy->kind),
                  busy->flags, busy->workspot);
      }
      for (const auto &message : Activities::Get().TakeOutbound())
        std::visit(
            [&](const auto &m) {
              using M = std::decay_t<decltype(m)>;
              if constexpr (std::is_same_v<M, SkipAsk>) {
                const auto request = endpoint->AskTimeSkip(m.reason, m.fromSeconds, m.toSeconds);
                log::Info("asked to skip time {} -> {} (request {}, reason {})", m.fromSeconds,
                          m.toSeconds, request, static_cast<int>(m.reason));
              } else if constexpr (std::is_same_v<M, SkipAnswer>) {
                endpoint->AnswerTimeSkip(m.asker, m.request, m.accepted, m.why);
              } else {
                endpoint->WithdrawTimeSkip();
              }
            },
            message);
      if (auto world = runtime.TakeWorld())
        endpoint->SetWorld(*world);
      for (const auto &message : runtime.TakeNpcOutbox()) {
        // (The record's owners follow at the next pass: `announced`.)
        if (std::holds_alternative<proto::NpcSpawn>(message) ||
            std::holds_alternative<proto::NpcGone>(message))
          announced = true;
        if (const auto *spawn = std::get_if<proto::NpcSpawn>(&message))
          endpoint->SendNpcSpawn(*spawn);
        else if (const auto *gone = std::get_if<proto::NpcGone>(&message))
          endpoint->SendNpcGone(*gone);
        else if (const auto *health = std::get_if<proto::NpcHealth>(&message))
          endpoint->SendNpcHealth(*health);
        else if (const auto *hit = std::get_if<proto::NpcHit>(&message))
          endpoint->SendNpcHit(*hit);
        else if (const auto *effect = std::get_if<proto::NpcEffect>(&message))
          endpoint->SendNpcEffect(*effect);
        else if (const auto *seat = std::get_if<proto::NpcSeat>(&message))
          endpoint->SendNpcSeat(*seat);
        else if (const auto *action = std::get_if<proto::NpcAction>(&message))
          endpoint->SendNpcAction(*action);
        else if (const auto *hold = std::get_if<proto::NpcHold>(&message))
          endpoint->SendNpcHold(*hold);
        else if (const auto *cue = std::get_if<proto::NpcCue>(&message))
          endpoint->SendNpcCue(*cue);
        else if (const auto *anims = std::get_if<proto::NpcAnims>(&message))
          endpoint->SendNpcAnims(*anims);
        else if (const auto *shot = std::get_if<proto::NpcShot>(&message))
          endpoint->SendNpcShot(*shot);
        else if (const auto *reaction = std::get_if<proto::NpcReaction>(&message))
          endpoint->SendNpcReaction(*reaction);
        else if (const auto *hit = std::get_if<proto::PlayerHit>(&message);
                 hit && !endpoint->SendPlayerHit(*hit))
          log::Info("hit on player {} dropped: not in the session, not our NPC's, or its game "
                    "does not show it",
                    hit->player);
        else if (const auto *shot = std::get_if<proto::NpcShotAt>(&message);
                 shot && !endpoint->SendNpcShotAt(*shot))
          log::Info("shot at player {} dropped: not in the session, not our NPC's, or its game "
                    "does not show it",
                    shot->player);
        else if (const auto *report = std::get_if<proto::ShotHit>(&message))
          endpoint->SendShotHit(*report);
      }
      for (const auto &message : runtime.TakeHackOutbox()) {
        if (const auto *hack = std::get_if<proto::NpcHack>(&message))
          endpoint->SendNpcHack(*hack);
        else if (const auto *state = std::get_if<proto::NpcHackState>(&message))
          endpoint->SendNpcHackState(*state);
        else if (const auto *status = std::get_if<proto::NpcStatus>(&message))
          endpoint->SendNpcStatus(*status);
        else if (const auto *player = std::get_if<proto::PlayerHack>(&message))
          endpoint->SendPlayerHack(*player);
        else if (const auto *car = std::get_if<proto::VehicleHack>(&message))
          endpoint->SendVehicleHack(*car);
        else if (const auto *street = std::get_if<proto::TrafficHack>(&message))
          endpoint->SendTrafficHack(*street);
      }
      for (const auto &[player, reply] : runtime.TakeNpcReplies())
        endpoint->SendNpcActionReply(player, reply);
      // Our answers on hand-overs, after what our game said of its NPCs (a
      // pin follows its spawn).
      for (auto &answer : Regions::Get().TakeAnswers()) {
        shared = true;
        if (auto *state = std::get_if<proto::NpcHandoverState>(&answer)) {
          log::Info("regions: npc {} handed over to player {}", state->npc, state->to);
          endpoint->GiveNpc(std::move(*state));
        } else if (const auto *refuse = std::get_if<RegionRefuse>(&answer)) {
          endpoint->RefuseNpc(refuse->npc);
        } else if (const auto *adopt = std::get_if<RegionAdopt>(&answer)) {
          if (!adopt->accepted)
            log::Warn("regions: npc {} could not be taken over, it goes", adopt->npc);
          endpoint->AdoptNpc(adopt->npc, adopt->accepted);
        } else if (const auto *pin = std::get_if<RegionPin>(&answer)) {
          endpoint->PinNpc(pin->npc);
        } else if (const auto *hold = std::get_if<RegionHeld>(&answer)) {
          log::Info("regions: our region is {}",
                    hold->held ? "held (a quest of our own runs here)" : "no longer held");
          endpoint->SetRegionHold(hold->held);
        }
      }
      if (shared && !ended) {
        // What our game shows as proxies (the NPCs of the owners linked with
        // us), and who runs every NPC of the session's record, ours too.
        runtime.SetShared(endpoint->ProxiedNpcs(), endpoint->Players());
        std::map<proto::NpcId, proto::PlayerId> owners;
        for (const auto &[npc, spawn] : endpoint->SharedNpcs())
          owners[npc] = endpoint->NpcOwner(npc);
        Regions::Get().SetOwners(std::move(owners));
        Regions::Get().SetLinked(endpoint->Linked());
        Regions::Get().SetGroup(endpoint->RegionLeader(), endpoint->RegionHolders());
        Regions::Get().SetAreas(endpoint->AreaOwners());
      }
      if (auto npcs = runtime.TakeNpcStates())
        endpoint->SendNpcStates(*npcs);
      // The street: what its owner says of its vehicles, then where they are.
      bool streetSaid = false;
      for (const auto &message : runtime.TakeStreetOutbox()) {
        if (const auto *spawn = std::get_if<proto::TrafficSpawn>(&message)) {
          endpoint->SendTrafficSpawn(*spawn);
          streetSaid = true;
        } else if (const auto *gone = std::get_if<proto::TrafficGone>(&message)) {
          endpoint->SendTrafficGone(*gone);
          runtime.ForgetTraffic(gone->id);
          streetSaid = true;
        } else if (const auto *status = std::get_if<proto::TrafficStatus>(&message)) {
          endpoint->SendTrafficStatus(*status);
          streetSaid = true;
        } else if (const auto *hit = std::get_if<proto::TrafficHit>(&message)) {
          endpoint->SendTrafficHit(*hit);
        } else if (const auto *claim = std::get_if<proto::TrafficClaim>(&message)) {
          endpoint->ClaimTraffic(claim->id, claim->seat);
          streetSaid = true;
        } else if (const auto *given = std::get_if<proto::TrafficHandoverState>(&message)) {
          log::Info("street: vehicle {:#x} given to player {}", given->id, given->to);
          endpoint->GiveTraffic(*given);
          streetSaid = true;
        } else if (const auto *done = std::get_if<proto::TrafficHandoverDone>(&message)) {
          endpoint->AnswerTraffic(done->id, done->accepted);
          streetSaid = true;
        } else if (const auto *crime = std::get_if<proto::Crime>(&message);
                   crime && !endpoint->SendCrime(*crime)) {
          log::Info("crime of player {} dropped: not a guest in the session", crime->player);
        }
      }
      if (streetSaid)
        ShareStreet(runtime, *endpoint);
      if (auto cars = runtime.TakeTrafficStates())
        endpoint->SendTrafficStates(std::move(*cars));
      // Vehicle combat: what our game says of its vehicles and our V in them.
      for (auto &message : runtime.TakeWheelsOutbox())
        std::visit(
            [&](auto &m) {
              using M = std::decay_t<decltype(m)>;
              if constexpr (std::is_same_v<M, proto::VehicleDamage>)
                endpoint->SetVehicleDamage(m);
              else if constexpr (std::is_same_v<M, proto::VehicleHit>)
                endpoint->SendVehicleHit(m);
              else if constexpr (std::is_same_v<M, proto::VehicleExit>)
                endpoint->SendVehicleExit(m);
              else if constexpr (std::is_same_v<M, proto::VehicleCombat>)
                endpoint->SetLocalVehicleCombat(m);
              else if constexpr (std::is_same_v<M, proto::MountedFire>)
                endpoint->SendMountedFire(m);
              else if constexpr (std::is_same_v<M, proto::GunnerRequest>)
                endpoint->AskGunner(m.vehicle, m.seat, m.leave);
              else if constexpr (std::is_same_v<M, proto::Gunner>)
                endpoint->FreeGunner(m.vehicle); // our game could not seat them
              else if constexpr (std::is_same_v<M, proto::GunnerAim>)
                endpoint->SendGunnerAim(m);
              else if constexpr (std::is_same_v<M, proto::VehicleBump>)
                endpoint->SendVehicleBump(m);
              else if constexpr (std::is_same_v<M, proto::NpcRam>)
                endpoint->SendNpcRam(m);
              else if constexpr (std::is_same_v<M, proto::Chase>)
                endpoint->SendChase(m);
            },
            message);
      if (auto heat = runtime.TakeLocalHeat()) {
        lastHeat = heat;
        if (heat->stage == 0)
          lastHeat.reset();
        endpoint->SetLocalHeat(*heat);
        log::Info("our wanted level {} (state {})", heat->stage, static_cast<int>(heat->state));
      }
      if (auto awareness = runtime.TakeNpcAwareness())
        endpoint->SendNpcAwareness(std::move(*awareness));
      if (auto stealth = runtime.TakeLocalStealth()) {
        lastStealth = *stealth;
        endpoint->SetLocalStealth(*stealth);
      }
      for (const auto &noise : runtime.TakeLocalNoises())
        endpoint->SendNoise(noise);
      for (auto &[message, to] : Story::Get().TakeOutbound())
        endpoint->SendStory(std::move(message), to);
      for (auto &message : Comms::Get().TakeOutbound()) {
        if (auto *mark = std::get_if<proto::Mark>(&message))
          endpoint->SendMark(*mark);
        else if (auto *tag = std::get_if<proto::Tag>(&message))
          endpoint->SendTag(std::move(*tag));
        else if (auto *link = std::get_if<proto::Link>(&message))
          endpoint->SetLocalLink(std::move(*link));
        else if (const auto *waypoint = std::get_if<proto::Waypoint>(&message))
          endpoint->SetLocalWaypoint(*waypoint);
        else if (const auto *decline = std::get_if<proto::LinkDecline>(&message))
          endpoint->SendLinkDecline(*decline);
      }
      // Other mods: what our game shows of others' records, names asked for.
      for (const auto &told : ModRecords::Get().TakeTold())
        endpoint->SendSubstitutes(told);
      for (const auto player : ModRecords::Get().TakeNameAsks())
        endpoint->AskModNames(player);
      for (auto &message : runtime.TakeRegroupOutbox()) {
        if (auto *offer = std::get_if<proto::Regroup>(&message))
          endpoint->SendRegroup(std::move(*offer));
        else if (const auto *answer = std::get_if<proto::RegroupAnswer>(&message))
          endpoint->SendRegroupAnswer(*answer);
      }
      for (auto &message : runtime.TakeDeviceOutbox()) {
        if (const auto *state = std::get_if<proto::DeviceState>(&message)) {
          if (state->how == proto::kDeviceAsk)
            endpoint->AskDevice(*state);
          else
            endpoint->ReportDevice(*state);
        } else if (const auto *use = std::get_if<proto::DeviceUse>(&message)) {
          endpoint->UseDevice(*use);
        }
      }
      for (const auto &message : runtime.TakeObjectOutbox()) {
        if (const auto *fracture = std::get_if<proto::Fracture>(&message))
          endpoint->SendFracture(*fracture);
        else if (const auto *fire = std::get_if<proto::DeviceFire>(&message))
          endpoint->SendDeviceFire(*fire);
        else if (const auto *hands = std::get_if<proto::TurretHands>(&message))
          endpoint->SendTurretHands(*hands);
      }
      for (auto &breach : runtime.TakeBreachOutbox()) {
        log::Info("breach: ours of network {:x} ({:#x}), npc {}: {} daemon(s), {} trap(s), "
                  "flags {:#x}",
                  breach.entity, breach.cls, breach.npc, breach.programs.size(),
                  breach.traps.size(), breach.flags);
        endpoint->SendBreach(std::move(breach));
      }
      for (auto &message : runtime.TakeLootOutbox()) {
        if (auto *item = std::get_if<proto::WorldItem>(&message))
          endpoint->DropWorldItem(std::move(*item));
        else if (const auto *taken = std::get_if<proto::WorldItemTaken>(&message))
          endpoint->TakeWorldItem(taken->id, taken->stack);
        else if (const auto *shared = std::get_if<proto::SharedItem>(&message))
          endpoint->SendSharedItem(*shared);
      }
      if (auto vitals = runtime.TakeLocalVitals()) {
        lastVitals = *vitals;
        endpoint->SetLocalVitals(*vitals);
        log::Info("our V: life {} (flags {:#x})", static_cast<int>(vitals->life), vitals->flags);
      }
      for (const auto &revive : runtime.TakeLocalRevives())
        endpoint->SendRevive(revive);
      if (auto presence = PresenceState::Get().TakeLocal()) {
        lastPresence = *presence;
        endpoint->SetLocalPresence(*presence);
      }
      for (const auto &cue : PresenceState::Get().TakeLocalCues())
        endpoint->SendLocalCue(cue);
      if (auto visuals = PresenceState::Get().TakeStatusVisuals()) {
        lastStatusVisuals = *visuals;
        endpoint->SetLocalStatusVisuals(std::move(*visuals));
      }
      if (!ended)
        PublishLives(*endpoint);
      for (auto &message : TradeLink::Get().TakeOutbound())
        endpoint->SendTrade(message);
      for (auto &message : Combat::Get().TakeOutbound()) {
        if (const auto *lend = std::get_if<proto::CarLend>(&message))
          lastLend = *lend;
        if (!endpoint->SendCombat(message))
          if (const auto *area = std::get_if<proto::AreaHit>(&message))
            log::Info("hit on player {} dropped: not in the session, or friendly fire is off",
                      area->player);
      }
      Combat::Get().SetRules(endpoint->Rules());

      stall::NetBeat(stall::kReport);
      if (const auto now = std::chrono::steady_clock::now(); now >= nextStatus && !ended) {
        nextStatus = now + 500ms;
        Publish(endpoint->IsJoined() ? SessionStatus::State::InSession
                                     : SessionStatus::State::Connecting,
                {}, endpoint.get());
      }
      if (const auto now = std::chrono::steady_clock::now();
          now >= nextMinute || runtime.TakePerfReport()) {
        const bool asked = now < nextMinute;
        if (!asked)
          nextMinute = now + 60s;
        const auto traffic = endpoint->Traffic();
        const auto delta = traffic - lastTraffic;
        const auto nowUs = net::NowUs();
        const double seconds = static_cast<double>(nowUs - lastTrafficUs) / 1e6;
        lastTraffic = traffic;
        lastTrafficUs = nowUs;
        const auto c = endpoint->Counters();
        const auto line = std::format(
            "traffic: {}; poses {} keys of {} frames ({} thinned), {} over another key; states "
            "held {}, thinned {}; npcs {} compact, {} whole",
            net::Summary(delta, seconds), c.poseKeys, c.poseFrames, c.poseSkipped, c.poseOtherKey,
            c.statesHeld, c.statesSkipped, c.npcMotions, c.npcWhole);
        const auto perf = std::format("perf: {}; queues at most: {}", Perf::Get().Report(),
                                      runtime.QueueReport());
        if (asked) {
          log::Always("{}", line);
          log::Always("{}", perf);
        } else {
          log::Info("{}", line);
          log::Info("{}", perf);
        }
        if (log::Enabled(log::Level::Debug))
          log::Debug("traffic table ({:.0f} s):\n{}", seconds, net::Table(delta, seconds));
      }
      if (const auto now = std::chrono::steady_clock::now(); now >= nextReport) {
        nextReport = now + 10s;
        const auto link = endpoint->HostLinkStats();
        if (link) {
          summary.rttMs = static_cast<std::int32_t>(link->rttMs);
          summary.lossPercent = link->packetLoss * 100;
        }
        log::Debug(
            "{} player(s){}", endpoint->Players().size(),
            link ? std::format(", rtt {} ms, loss {:.1f}%", link->rttMs, link->packetLoss * 100)
                 : "");
        // Consistency: only when something happened.
        if (const auto &checks = endpoint->Checks(); checks.Requests() != reportedRequests) {
          reportedRequests = checks.Requests();
          log::Info("consistency: {} digest mismatch(es), {} resync(s) asked", checks.Mismatches(),
                    checks.Requests());
        }
        if (endpoint->Fenced())
          log::Info("consistency: {} guest request(s) aimed at an older world dropped",
                    endpoint->Fenced());
        for (const auto &[id, info] : endpoint->Players()) {
          if (id == endpoint->You())
            continue;
          if (const auto stats = runtime.StatsOf(id))
            log::Debug("  player {}: {} states, {} dropped, {} poses, drawn {} ms behind (jitter "
                       "{} ms, transit {} ms, every {} ms)",
                       id, stats->received, stats->dropped, runtime.PosesReceived(id),
                       stats->delayUs / 1000, stats->jitterUs / 1000, stats->transitUs / 1000,
                       stats->intervalUs / 1000);
        }
      }
    }
    runtime.SetYou(0);
    // Nobody in this session pauses us any more; its code is gone.
    runtime.SetRemotePaused({});
    runtime.ClearRemoteElsewhere();
    Activities::Get().EndSession();
    runtime.SetRelay("", "");
    summary.relay = endpoint->ThroughRelay() || (endpoint->IsHost() && relay.has_value());
    summary.seconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                     std::chrono::steady_clock::now() - startedAt)
                                                     .count());
    summary.ended = !endReason.empty()      ? endReason
                    : stop.stop_requested() ? "left (the co-op menu, or the game closed)"
                                            : "the session ended";
    summary.mismatches = endpoint->Checks().Mismatches();
    summary.resyncs = endpoint->Checks().Requests();
    // For the bug report (tandem/report.hpp): the last session only. Not a
    // guest's tries at a host that is gone: they would write over the
    // session that mattered every 5 s.
    if (wasIn || !refusal.empty())
      if (std::ofstream file(config.commandFile.parent_path() / "last-session.txt",
                             std::ios::trunc);
          file)
        file << report::Format(summary);
    // After the session's own reset, so the menu still says it.
    if (!refusal.empty())
      runtime.NoteDifference(refusal);
    endpoint->Leave(stop.stop_requested() ? "left the session" : "the game closed");
    // Stopped (the co-op menu's Leave, another choice, the game left): the
    // story side ends its session too (a guest gets its own story back).
    if (stop.stop_requested() && !ended) {
      Story::Get().PushInbound({.kind = StoryInbound::Kind::Ended});
      Comms::Get().PushInbound({.kind = CommsInbound::Kind::Ended});
    }
    return endReason.empty() ? std::string("the session ended") : endReason;
  }
};

NetThread::NetThread(Config config, proto::Body body) : m_impl(std::make_unique<Impl>()) {
  m_impl->config = std::move(config);
  m_impl->body = body;
  m_impl->thread = std::jthread([impl = m_impl.get()](std::stop_token stop) { impl->Run(stop); });
}

NetThread::~NetThread() {
  m_impl->thread.request_stop();
  if (m_impl->thread.joinable())
    m_impl->thread.join();
}
} // namespace tandem::plugin
