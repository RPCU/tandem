#include "tandem/sim/persona.hpp"

namespace tandem::sim {
namespace {
Persona Make(std::string name, std::string what,
             std::initializer_list<std::pair<Goal, float>> weights) {
  Persona persona{.name = std::move(name), .what = std::move(what)};
  for (const auto &[goal, weight] : weights)
    persona.weights[static_cast<std::size_t>(goal)] = weight;
  return persona;
}

const std::vector<Persona> &Personas() {
  static const std::vector<Persona> personas = [] {
    std::vector<Persona> out;
    // Stays near, helps in fights: the partner one plays with.
    out.push_back(Make("companion", "stays near its partner and helps in fights",
                       {{Goal::Follow, 6.f},
                        {Goal::Wander, 1.f},
                        {Goal::Fight, 8.f},
                        {Goal::Hack, 3.f},
                        {Goal::Sandevistan, 2.f},
                        {Goal::Takedown, 3.f},
                        {Goal::Drive, 1.f},
                        {Goal::Ride, 4.f},
                        {Goal::FastTravel, 0.5f},
                        {Goal::Regroup, 0.5f},
                        {Goal::Metro, 0.3f},
                        {Goal::Elsewhere, 0.3f},
                        {Goal::Pause, 0.2f},
                        {Goal::Camo, 1.f},
                        {Goal::DropItem, 1.f},
                        {Goal::LootQuest, 0.5f},
                        {Goal::GoDown, 0.6f},
                        {Goal::CarHack, 0.2f},
                        {Goal::Dial, 0.2f}}));
    // Far away, in its own region: a session of two players apart.
    auto loner = Make("loner", "plays far away, in its own region",
                      {{Goal::Wander, 6.f},
                       {Goal::Fight, 4.f},
                       {Goal::Hack, 2.f},
                       {Goal::Sandevistan, 2.f},
                       {Goal::Takedown, 2.f},
                       {Goal::Drive, 4.f},
                       {Goal::FastTravel, 1.f},
                       {Goal::Metro, 1.f},
                       {Goal::Elsewhere, 1.f},
                       {Goal::Pause, 0.2f},
                       {Goal::Camo, 1.f},
                       {Goal::DropItem, 0.5f},
                       {Goal::LootQuest, 0.5f},
                       {Goal::GoDown, 0.3f},
                       {Goal::Dial, 0.3f}});
    loner.region = 400.f;
    loner.helps = false;
    loner.comes = false;
    out.push_back(std::move(loner));
    // Legal, at the worst time: acts while the other loads, leaves and
    // joins again, vanishes, asks for what just stopped being possible, on
    // a link that loses a seventh of its datagrams.
    auto chaos =
        Make("chaos", "legal but adversarial: bad timing, leaving, vanishing, a lossy link",
             {{Goal::Follow, 3.f},    {Goal::Wander, 2.f},      {Goal::Fight, 8.f},
              {Goal::Hack, 5.f},      {Goal::Sandevistan, 4.f}, {Goal::Takedown, 6.f},
              {Goal::Drive, 2.f},     {Goal::Ride, 2.f},        {Goal::FastTravel, 3.f},
              {Goal::Regroup, 2.f},   {Goal::Metro, 1.f},       {Goal::Elsewhere, 2.f},
              {Goal::Pause, 1.f},     {Goal::Camo, 2.f},        {Goal::DropItem, 3.f},
              {Goal::LootQuest, 1.f}, {Goal::GoDown, 3.f},      {Goal::CarHack, 2.f},
              {Goal::Grenade, 1.f},   {Goal::Dial, 1.f}});
    chaos.tempo = 2.f;
    chaos.hostile = true;
    chaos.worstMoments = true;
    chaos.leaveEverySeconds = 45.;
    chaos.vanishShare = 0.5;
    chaos.link = {.latencyMs = 40, .jitterMs = 60, .loss = 0.15f, .reorder = 0.05f};
    out.push_back(std::move(chaos));
    // Always somewhere else: fast travel, cars, the metro.
    auto speedrunner = Make("speedrunner", "always on the move: fast travel, vehicles, the metro",
                            {{Goal::Follow, 1.f},
                             {Goal::Fight, 2.f},
                             {Goal::Sandevistan, 3.f},
                             {Goal::Drive, 8.f},
                             {Goal::Ride, 1.f},
                             {Goal::FastTravel, 8.f},
                             {Goal::Regroup, 2.f},
                             {Goal::Metro, 4.f},
                             {Goal::DropItem, 0.5f},
                             {Goal::LootQuest, 0.5f}});
    speedrunner.tempo = 1.5f;
    out.push_back(std::move(speedrunner));
    return out;
  }();
  return personas;
}
} // namespace

const char *NameOf(Goal goal) {
  constexpr const char *names[] = {
      "follow",    "wander",      "fight",   "hack",     "sandevistan", "takedown", "drive",
      "ride",      "fast-travel", "regroup", "metro",    "elsewhere",   "pause",    "camo",
      "drop-item", "loot-quest",  "go-down", "car-hack", "grenade",     "dial"};
  static_assert(std::size(names) == static_cast<std::size_t>(Goal::Count));
  return names[static_cast<std::size_t>(goal)];
}

const Persona *PersonaNamed(std::string_view name) {
  for (const auto &persona : Personas())
    if (persona.name == name)
      return &persona;
  return nullptr;
}

std::vector<std::string> PersonaNames() {
  std::vector<std::string> names;
  for (const auto &persona : Personas())
    names.push_back(persona.name);
  return names;
}
} // namespace tandem::sim
