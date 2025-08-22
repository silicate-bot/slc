#ifndef SLC_UTIL_HPP
#define SLC_UTIL_HPP

#include <bit>
#include <iostream>
#include <span>

#define SLC_NS_BEGIN namespace slc {
#define SLC_NS_END }

SLC_NS_BEGIN

namespace util {
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
