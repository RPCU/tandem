#pragma once

// A std::variant whose own code is compiled once. libstdc++'s variant of N
// alternatives instantiates O(N²) templates for each of its operations
// (construction from an alternative, copy, move, destruction: one recursion
// level per alternative); with ~140 messages that is over a GB of compiler
// memory in every translation unit that copies one. ClosedVariant<V>
// declares those operations here and defines them in one translation unit
// (closed_variant_impl.hpp, explicitly instantiated there: message.cpp for
// proto::Message, events.cpp for SessionEvent). Everything else of the
// variant works as on V, of which it is a public base: std::visit,
// std::get_if, std::holds_alternative, index().

#include <cstddef>
#include <memory>
#include <type_traits>
#include <variant>

namespace tandem {

namespace detail {
// The index of T among Ts (sizeof...(Ts): not there, or there twice).
template <class T, class... Ts> consteval std::size_t IndexOf() {
  constexpr bool same[] = {std::is_same_v<T, Ts>...};
  std::size_t found = sizeof...(Ts);
  for (std::size_t i = 0; i < sizeof...(Ts); ++i)
    if (same[i]) {
      if (found != sizeof...(Ts))
        return sizeof...(Ts);
      found = i;
    }
  return found;
}
template <class T, class V> struct IndexIn;
template <class T, class... Ts>
struct IndexIn<T, std::variant<Ts...>> : std::integral_constant<std::size_t, IndexOf<T, Ts...>()> {
};
} // namespace detail

template <class V> class ClosedVariant : public V {
public:
  ClosedVariant();
  ClosedVariant(const ClosedVariant &);
  ClosedVariant(ClosedVariant &&) noexcept;
  ClosedVariant &operator=(const ClosedVariant &);
  ClosedVariant &operator=(ClosedVariant &&) noexcept;
  ~ClosedVariant();

  // From one of V's alternatives, copied or moved.
  template <class T, std::size_t I = detail::IndexIn<std::remove_cvref_t<T>, V>::value>
    requires(I < std::variant_size_v<V>)
  ClosedVariant(T &&alternative)
      : ClosedVariant(I, const_cast<void *>(static_cast<const void *>(std::addressof(alternative))),
                      std::is_rvalue_reference_v<T &&> &&
                          !std::is_const_v<std::remove_reference_t<T>>) {}

private:
  ClosedVariant(std::size_t index, void *from, bool move);
};

} // namespace tandem

template <class V> struct std::variant_size<tandem::ClosedVariant<V>> : std::variant_size<V> {};
template <std::size_t I, class V>
struct std::variant_alternative<I, tandem::ClosedVariant<V>> : std::variant_alternative<I, V> {};
