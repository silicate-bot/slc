#ifndef _SLC_V3_METADATA_HPP
#define _SLC_V3_METADATA_HPP

#include "slc/util.hpp"

#include <expected>
#include <string>

SLC_NS_BEGIN

namespace v3 {

constexpr uint64_t METADATA_SIZE = 64;

struct Metadata {
  double m_tps;
  uint64_t m_seed;

  uint32_t m_checksum;
  uint32_t m_build;

  char __padding[40] = {0};
};
static_assert(sizeof(Metadata) == METADATA_SIZE);

} // namespace v3

SLC_NS_END

#endif
