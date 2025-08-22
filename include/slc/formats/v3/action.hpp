#ifndef _SLC_V3_ACTION_HPP
#define _SLC_V3_ACTION_HPP

#include "slc/util.hpp"

SLC_NS_BEGIN

namespace v3 {

/**
 * Public-facing input type.
 *
 * Use this to execute actions in your bot.
 */
class Action {
private:
  uint64_t m_delta;
#if USE_DELTA_DIFFERENCES
  bool m_difference;
#endif

public:
  enum class ActionType : uint8_t {
    Reserved = 0,

    // Player
    Jump = 1,
    Left = 2,
    Right = 3,

    // Death-related (all three can change seed)
    Restart = 4,
    RestartFull = 5,
    Death = 6,

    // TPS
    TPS = 7,
  };

  /** The frame on which this action should be executed. */
  uint64_t m_frame;
  /** The type of the action. */
  ActionType m_type;

  // Additional metadata

  /**
   * Used for Player actions (1-3).
   * Whether this is a hold or a release.
   */
  bool m_holding = false;

  /**
   * Used for Player actions (1-3).
   * Whether this is for player 1 or 2.
   */
  bool m_player2 = false;

  /**
   * Used for Death actions (4-6).
   * The seed to set the replay to.
   */
  uint64_t m_seed = 0;

  /**
   * Used for TPS actions (7).
   * The seed to set the replay to.
   */
  double m_tps = 240.0;

  /**
   * Don't set this.
   * Used for internal optimization.
   */
  bool m_swift = false;

#if USE_DELTA_DIFFERENCES
  inline const bool useDifference() const { return m_difference; }
#else
  inline const bool useDifference() const { return false; }
#endif
  inline const bool swift() const { return m_swift; }

  const uint8_t getMinimumSize(uint64_t dd) const {
#if USE_DELTA_DIFFERENCES
    uint64_t offset = 3;
    const uint64_t delta = m_difference ? m_delta : dd;
    if (static_cast<int>(m_type) >= 4) {
      offset = 7;
    }
#else
    uint64_t offset = 4ull;
    const uint64_t delta = m_delta;
    // Special
    if (static_cast<int>(m_type) >= 4) {
      offset = 8;
    }
#endif

    const uint64_t ONE_BYTE_THRESHOLD = 1ull << (offset);
    const uint64_t TWO_BYTES_THRESHOLD = 1ull << (offset + 8ull);
    const uint64_t FOUR_BYTES_THRESHOLD = 1ull << (offset + 24ull);

    if (delta < ONE_BYTE_THRESHOLD) {
      return 0;
    } else if (delta < TWO_BYTES_THRESHOLD) {
      return 1;
    } else if (delta < FOUR_BYTES_THRESHOLD) {
      return 2;
    } else {
      return 3;
    }
  }

  inline const bool isPlayer() const {
    return static_cast<uint8_t>(m_type) <= 3;
  }

  void recalculateDelta(uint64_t previousFrame) {
    m_delta = m_frame - previousFrame;
  }
  inline const uint64_t delta() const { return m_delta; }

  Action() = default;
  Action(uint64_t currentFrame, uint64_t delta, ActionType button, bool holding,
         bool p2) {
    m_frame = currentFrame + delta;
    m_type = button;
    m_holding = holding;
    m_player2 = p2;
    m_delta = delta;
  }
};

} // namespace v3

SLC_NS_END

#endif
