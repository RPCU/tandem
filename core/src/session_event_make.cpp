// SessionEvent from one of its alternatives (session_event.cpp).
#include "closed_variant_impl.hpp"
#include "tandem/endpoint.hpp"

template tandem::ClosedVariant<tandem::SessionEventVariant>::ClosedVariant(std::size_t, void *,
                                                                           bool);
