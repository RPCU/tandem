// SessionEvent's own code (tandem/closed_variant.hpp), compiled here and in
// session_event_make.cpp only, as proto::Message's (message.cpp).
#include "closed_variant_impl.hpp"
#include "tandem/endpoint.hpp"

namespace tandem {
template ClosedVariant<SessionEventVariant>::ClosedVariant();
template ClosedVariant<SessionEventVariant>::ClosedVariant(const ClosedVariant &);
template ClosedVariant<SessionEventVariant>::ClosedVariant(ClosedVariant &&) noexcept;
template ClosedVariant<SessionEventVariant>::~ClosedVariant();
template ClosedVariant<SessionEventVariant> &
ClosedVariant<SessionEventVariant>::operator=(const ClosedVariant &);
template ClosedVariant<SessionEventVariant> &
ClosedVariant<SessionEventVariant>::operator=(ClosedVariant &&) noexcept;
} // namespace tandem
