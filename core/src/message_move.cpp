// proto::Message's move (message.cpp).
#include "closed_variant_impl.hpp"
#include "tandem/protocol.hpp"

template tandem::ClosedVariant<tandem::proto::MessageVariant>::ClosedVariant(
    ClosedVariant &&) noexcept;
