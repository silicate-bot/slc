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

  uint32_t m_version = 2;
  uint32_t m_build;

  /**
   * What randomness algorithm to use for variance.
   *
   * 0 = Current algorithm - uses array indices in trigger instance and random
   * seed to determine variance index. Generates variance values based on random
   * seed.
   */
  uint32_t m_randomnessAlgorithm = 0;

  char __padding[36] = {0};
};

static_assert(sizeof(Metadata) == METADATA_SIZE);

} // namespace v3

SLC_NS_END

#endif
