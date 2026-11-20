#pragma once

// ClosedVariant's operations (tandem/closed_variant.hpp). Included only by
// the translation unit that explicitly instantiates a ClosedVariant:
//   template class tandem::ClosedVariant<SomeVariant>;

#include "tandem/closed_variant.hpp"

#include <array>
#include <new>
#include <utility>

namespace tandem {

// Written out rather than defaulted, so that each can be instantiated in a
// translation unit of its own: with GCC, the copy and the move each take
// some 1.5 GB of compiler memory for ~140 alternatives, construction from an
// alternative 2 GB, and the variant's own assignments over 5 GB (they are
// not used: assigning is destroying and constructing).
template <class V> ClosedVariant<V>::ClosedVariant() : V() {}
template <class V>
ClosedVariant<V>::ClosedVariant(const ClosedVariant &other) : V(static_cast<const V &>(other)) {}
template <class V>
ClosedVariant<V>::ClosedVariant(ClosedVariant &&other) noexcept : V(static_cast<V &&>(other)) {}
template <class V> ClosedVariant<V>::~ClosedVariant() {}

// A copy that throws leaves *this as it was; a move does not throw.
template <class V> ClosedVariant<V> &ClosedVariant<V>::operator=(const ClosedVariant &other) {
  if (this != &other)
    *this = ClosedVariant(other);
  return *this;
}
template <class V> ClosedVariant<V> &ClosedVariant<V>::operator=(ClosedVariant &&other) noexcept {
  if (this != &other) {
    this->~ClosedVariant();
    ::new (static_cast<void *>(this)) ClosedVariant(static_cast<ClosedVariant &&>(other));
  }
  return *this;
}

namespace detail {
template <class V> V MakeAlternative(std::size_t index, void *from, bool move) {
  using Make = V (*)(void *, bool);
  static constexpr auto table = []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<Make, sizeof...(I)>{+[](void *from, bool move) -> V {
      using T = std::variant_alternative_t<I, V>;
      if (move)
        return V(std::in_place_index<I>, std::move(*static_cast<T *>(from)));
      return V(std::in_place_index<I>, *static_cast<const T *>(from));
    }...};
  }(std::make_index_sequence<std::variant_size_v<V>>{});
  return table[index](from, move);
}
} // namespace detail

template <class V>
ClosedVariant<V>::ClosedVariant(std::size_t index, void *from, bool move)
    : V(detail::MakeAlternative<V>(index, from, move)) {}

} // namespace tandem
