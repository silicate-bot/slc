#ifndef _SLC_V3_ERROR_HPP
#define _SLC_V3_ERROR_HPP

#include "slc/util.hpp"

#include <expected>
#include <string>

SLC_NS_BEGIN

namespace v3 {

#define TRY(failable)                                                          \
  ({                                                                           \
    auto result = (failable);                                                  \
    if (!result)                                                               \
      return std::unexpected{result.error()};                                  \
    *result;                                                                   \
  })

struct Error {
  // Concise error description.
  std::string m_message;

  Error(std::string msg) : m_message(msg) {}
  // std::string is constructible from const char*
  Error(const char *msg) : m_message(msg) {}
};

template <typename T = void> using Result = std::expected<T, Error>;

} // namespace v3

SLC_NS_END

#endif
