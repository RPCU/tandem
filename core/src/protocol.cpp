#include "tandem/protocol.hpp"

#include <zpp_bits.h>

namespace tandem::proto {
std::vector<std::byte> Encode(const Message &message) {
  std::vector<std::byte> bytes;
  auto out = zpp::bits::out(bytes);
  if (zpp::bits::failure(out(message)))
    return {};
  return bytes;
}

std::optional<Message> Decode(std::span<const std::byte> bytes) {
  Message message;
  auto in = zpp::bits::in(bytes);
  if (zpp::bits::failure(in(message)))
    return std::nullopt;
  // A message must be read whole: trailing bytes mean the two sides disagree.
  if (in.position() != bytes.size())
    return std::nullopt;
  if (const auto *appearance = std::get_if<Appearance>(&message);
      appearance && (appearance->look.size() > kMaxAppearanceKeys ||
                     appearance->items.size() > kMaxAppearanceItems))
    return std::nullopt;
  if (const auto *npcs = std::get_if<NpcStates>(&message);
      npcs && npcs->npcs.size() > kMaxNpcsPerState)
    return std::nullopt;
  if (const auto *effect = std::get_if<NpcEffect>(&message);
      effect && effect->effects.size() > kMaxNpcEffects)
    return std::nullopt;
  return message;
}

Channel ChannelOf(const Message &message) {
  return std::holds_alternative<PlayerState>(message) ||
                 std::holds_alternative<PoseState>(message) ||
                 std::holds_alternative<Shot>(message) ||
                 std::holds_alternative<VehicleState>(message) ||
                 std::holds_alternative<NpcStates>(message)
             ? Channel::State
             : Channel::Event;
}

bool IsReliable(const Message &message) { return ChannelOf(message) != Channel::State; }

const char *NameOf(const Message &message) {
  constexpr const char *names[] = {
      "Hello",       "Welcome",      "Refused",   "Ping",       "Pong",      "Roster",
      "PlayerState", "Bye",          "PoseState", "Appearance", "Held",      "Shot",
      "Seat",        "VehicleState", "World",     "NpcSpawn",   "NpcStates", "NpcGone",
      "NpcHit",      "NpcHealth",    "NpcEffect", "Pause",      "PlayerHit"};
  static_assert(std::size(names) == std::variant_size_v<Message>);
  return names[message.index()];
}
} // namespace tandem::proto
