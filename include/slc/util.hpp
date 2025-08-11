#ifndef SLC_UTIL_HPP
#define SLC_UTIL_HPP

#include <iostream>

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
} // namespace _util

SLC_NS_END

#endif // SLC_UTIL_HPP