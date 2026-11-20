// proto::Message from one of its alternatives (message.cpp).
#include "closed_variant_impl.hpp"
#include "tandem/protocol.hpp"

template tandem::ClosedVariant<tandem::proto::MessageVariant>::ClosedVariant(std::size_t, void *,
                                                                             bool);
