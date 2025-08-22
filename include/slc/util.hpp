#ifndef SLC_UTIL_HPP
#define SLC_UTIL_HPP

#include <bit>
#include <iostream>
#include <span>

#define SLC_NS_BEGIN namespace slc {
#define SLC_NS_END }

SLC_NS_BEGIN

namespace util {

template <typename T, T Left, T Right>
  requires std::totally_ordered<T>
constexpr bool inRange(T value) {
  static_assert(Left <= Right, "Lhs must be lesser or equal to Rhs");

  return value >= Left && value <= Right;
}

template <typename T> T binRead(std::istream &s) {
  T temp;

  s.read(reinterpret_cast<char *>(&temp), sizeof(T));

  return temp;
}

template <typename T> void binWrite(std::ostream &s, const T &val) {
  s.write(reinterpret_cast<const char *>(&val), sizeof(T));
}

template <typename T>
  requires std::is_integral_v<T>
int exponentOfTwo(T n) {
  return n ? std::min(
                 static_cast<int>((sizeof(T) * 8 - 1 - std::countl_zero(n))),
                 15)
           : 0;
}

template <typename T>
  requires std::is_integral_v<T>
int largestPowerOfTwo(T n) {
  return n ? 1ULL << exponentOfTwo(n) : 0;
}
} // namespace util

SLC_NS_END

#endif // SLC_UTIL_HPP
