#ifndef SLC_FORMATS_V3_HPP
#define SLC_FORMATS_V3_HPP

#include "slc/util.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

SLC_NS_BEGIN

namespace v3 {

///////////////////////////////////////
//////////// PUBLIC FACING ////////////
///////////////////////////////////////

/**
 * Public-facing input type.
 *
 * Use this to execute actions in your bot.
 */
class Action {
private:
  uint64_t m_delta;

public:
  enum class ActionType : uint8_t {
    Skip = 0,

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
   * Used for Player actions (0-3).
   * Whether this is a hold or a release.
   */
  bool m_holding = false;

  /**
   * Used for Player actions (0-3).
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

  const uint8_t getMinimumSize() const {
    const uint64_t ONE_BYTE_THRESHOLD = 1 << 4;
    const uint64_t TWO_BYTES_THRESHOLD = 1 << 12;
    const uint64_t FOUR_BYTES_THRESHOLD = 1 << 28;

    if (m_delta < ONE_BYTE_THRESHOLD) {
      return 1;
    } else if (m_delta < TWO_BYTES_THRESHOLD) {
      return 2;
    } else if (m_delta < FOUR_BYTES_THRESHOLD) {
      return 4;
    } else {
      return 8;
    }
  }

  inline const bool isPlayer() const {
    return static_cast<uint8_t>(m_type) > 0;
  }

  void recalculateDelta(uint64_t previousFrame) {
    m_delta = m_frame - previousFrame;
  }
  inline const uint64_t delta() const { return m_delta; }
};

///////////////////////////////////////
//////////// INTERNALS ////////////////
///////////////////////////////////////

class PlayerInput {
public:
  enum class Button : uint8_t {
    Skip = 0,
    Jump = 1,
    Left = 2,
    Right = 3,
  };

  uint64_t m_frame;
  uint64_t m_delta;
  Button m_button;
  bool m_holding;
  bool m_player2;

  static PlayerInput fromAction(const Action &action) {
    assert(action.isPlayer());

    PlayerInput p;
    p.m_button = static_cast<Button>(action.m_type);
    p.m_frame = action.m_frame;
    p.m_delta = action.delta();
    p.m_holding = action.m_holding;
    p.m_player2 = action.m_player2;

    return p;
  }
};

class Section {
public:
  enum class Identifier : uint8_t {
    /**
     * XX XX    XXXX
     * -- --    ----
     * ID Size Count (2^X)
     */
    Input = 0,
    /**
     * XX XX   XXXX   XXXX    XXXX
     * -- --   ----   ----    ----
     * ID Size Count Repeats Reserved
     */
    Repeat,
    /**
     * XX XXXX XX
     * -- ---- --
     * ID Type Size
     */
    Special,
  };

  enum class SpecialType : uint8_t { Restart = 0, RestartFull, Death, TPS };

private:
  Identifier m_id;

  // Player
  std::vector<PlayerInput> m_playerInputs;
  uint8_t m_countExp;
  uint8_t m_repeatsExp;

  // Special
  SpecialType m_specialType;
  uint64_t m_seed;
  double m_tps;
  Action m_special;

protected:
public:
  uint8_t m_deltaSize;
  bool m_markedForRemoval;

  uint64_t getRealDeltaSize() {
    assert(m_deltaSize <= 3);

    return 1ull << (uint64_t)m_deltaSize;
  }
  uint64_t getInputCount() { return 1ull << (uint64_t)m_countExp; }
  uint64_t getRepeatCount() { return 1ull << (uint64_t)m_repeatsExp; }
  inline const bool isSpecial() const { return m_id == Identifier::Special; }

  void copyFrom(Section &other) {
    assert(!isSpecial());

    m_playerInputs.insert(m_playerInputs.end(), other.m_playerInputs.begin(),
                          other.m_playerInputs.end());
  }

  size_t totalSize() {
    switch (m_id) {
    case Identifier::Input: {
      return getInputCount() * getRealDeltaSize() + 1;
    }
    case Identifier::Repeat: {
      return getInputCount() * getRealDeltaSize() * getRepeatCount() + 2;
    }
    case Identifier::Special: {
      return 1 + 8 + getRealDeltaSize();
    }
    }

    return 0; // unreachable
  }

  size_t newSizeAssumingDeltaSize(uint64_t size) {
    switch (m_id) {
    case Identifier::Input: {
      return getInputCount() * size + 1;
    }
    case Identifier::Repeat: {
      return getInputCount() * size * getRepeatCount() + 2;
    }
    case Identifier::Special: {
      return 1 + 8 + size;
    }
    }

    return 0; // unreachable
  }

  static Section player(const Action &start) {
    assert(start.isPlayer());
    Section s;

    s.m_id = Identifier::Input;
    s.m_playerInputs = {PlayerInput::fromAction(start)};

    return s;
  }

  void addPlayerInput(const Action &action) {
    m_playerInputs.push_back(PlayerInput::fromAction(action));
  }

  static Section special(const Action &action) {
    assert(!action.isPlayer());
    Section s;

    s.m_id = Identifier::Special;

    using A = Action::ActionType;

    switch (action.m_type) {
    case A::TPS: {
      assert(action.m_tps > 0);

      s.m_tps = action.m_tps;
      s.m_specialType = SpecialType::TPS;
      break;
    };
    case A::Death:
    case A::Restart:
    case A::RestartFull: {
      s.m_seed = action.m_seed;
      s.m_specialType =
          static_cast<SpecialType>(static_cast<int>(action.m_type) - 4);
      break;
    }
    default:
      throw std::runtime_error("Invalid action to create a special section - "
                               "somehow got past assertion");
    }

    s.m_special = action;

    return s;
  }
};

constexpr uint64_t METADATA_SIZE = 64;

struct Metadata {
  double m_tps;
  uint64_t m_seed;

  uint32_t m_checksum;
  uint32_t m_build;

  char __padding[40] = {0};
};
static_assert(sizeof(Metadata) == METADATA_SIZE);

class Replay {
private:
  Metadata m_meta;
  std::deque<Action> m_actions;

private:
  // "literally slc2"
  void prepareSections(std::vector<Section> &sections) const {
    for (const auto &action : m_actions) {
      const uint8_t size = action.getMinimumSize();
      if (!action.isPlayer()) {
        sections.push_back(Section::special(action));
        return;
      }

      if (sections.empty() || sections.back().isSpecial()) {
        sections.push_back(Section::player(action));
        continue;
      }

      auto &previous = sections.back();
      if (previous.m_deltaSize != action.getMinimumSize()) {
        sections.push_back(Section::player(action));
      } else {
        previous.addPlayerInput(action);
      }
    }
  }

  // Optimization passes
  void contractSections(std::vector<Section> &sections) const {
    for (int i = sections.size() - 1; i > 0; i++) {
      auto &section = sections[i];
      auto &previous = sections[i - 1];

      size_t currentSize = section.totalSize() - sizeof(Section);
      bool isTiny = currentSize < sizeof(Section);

      if (isTiny) {
        if (section.m_deltaSize > previous.m_deltaSize &&
            section.newSizeAssumingDeltaSize(previous.m_deltaSize) <
                sizeof(Section)) {
        }
      }
    }
  }

public:
  static constexpr size_t HEADER_SIZE = 4;
  static constexpr char HEADER[HEADER_SIZE] = {'S', 'L', 'C', '3'};

  void write(std::ostream &out) const {
    out.write(HEADER, HEADER_SIZE);

    uint64_t metaSize = sizeof(Metadata);
    util::binWrite(out, metaSize);
    util::binWrite(out, m_meta);

    uint64_t actionCount = m_actions.size();
    util::binWrite(out, actionCount);

    std::vector<Section> sections;

    sections.reserve(
        actionCount); // worst case scenario; no optimizations will be applied

    prepareSections(sections);  // Necessary for the replay to save; organizes
                                // actions into sections
    contractSections(sections); // Contracts similar-sized sections to optimize
                                // for byte size, removes unnecessary sections
  }
};

}; // namespace v3

SLC_NS_END

#endif // SLC_FORMATS_V3_HPP
