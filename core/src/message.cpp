// proto::Message's own code (tandem/closed_variant.hpp), compiled here and in
// message_move.cpp and message_make.cpp only: each takes GBs of compiler
// memory with ~140 alternatives, so each has a translation unit of its own.
#include "closed_variant_impl.hpp"
#include "tandem/protocol.hpp"

namespace tandem {
using proto::MessageVariant;
template ClosedVariant<MessageVariant>::ClosedVariant();
template ClosedVariant<MessageVariant>::ClosedVariant(const ClosedVariant &);
template ClosedVariant<MessageVariant>::~ClosedVariant();
template ClosedVariant<MessageVariant> &
ClosedVariant<MessageVariant>::operator=(const ClosedVariant &);
template ClosedVariant<MessageVariant> &
ClosedVariant<MessageVariant>::operator=(ClosedVariant &&) noexcept;
} // namespace tandem

namespace tandem::proto {
bool operator==(const Message &a, const Message &b) {
  return static_cast<const MessageVariant &>(a) == static_cast<const MessageVariant &>(b);
}
} // namespace tandem::proto
