#ifndef SLC_FORMATS_V3_HPP
#define SLC_FORMATS_V3_HPP

#include "slc/util.hpp"
#include <bit>
#include <cassert>
#include <deque>
#include <iostream>
#ifdef SLC_INSPECT
#include <print>
#endif
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

SLC_NS_BEGIN

#define USE_DELTA_DIFFERENCES 0

namespace v3 {

///////////////////////////////////////
//////////// PUBLIC FACING ////////////
///////////////////////////////////////

class Replay;

/**
 * Public-facing input type.
 *
 * Use this to execute actions in your bot.
 */
class Action {
private:
  uint64_t m_delta;
  bool m_swift = false;
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
    uint64_t offset = 4;
    const uint64_t delta = m_delta;
    // Special
    if (static_cast<int>(m_type) >= 4) {
      offset = 8;
    }
#endif

    const uint64_t ONE_BYTE_THRESHOLD = 1 << (offset);
    const uint64_t TWO_BYTES_THRESHOLD = 1 << (offset + 8);
    const uint64_t FOUR_BYTES_THRESHOLD = 1 << (offset + 24);

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
    return static_cast<uint8_t>(m_type) > 0;
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

  friend Replay;
};

///////////////////////////////////////
//////////// INTERNALS ////////////////
///////////////////////////////////////

class PlayerInput {
public:
  enum class Button : uint8_t {
    Swift = 0,
    Jump = 1,
    Left = 2,
    Right = 3,
  };

  uint64_t m_frame;
  uint64_t m_delta;
  Button m_button;
  bool m_holding;
  bool m_player2;
  bool m_difference;

  static PlayerInput fromAction(const Action &action, uint64_t dd) {
    assert(action.isPlayer());

    PlayerInput p;
    p.m_button = static_cast<Button>(action.m_type);
    if (action.swift()) {
      p.m_button = Button::Swift;
    }
    p.m_frame = action.m_frame;
    p.m_delta = action.useDifference() ? dd : action.delta();
    p.m_holding = action.m_holding;
    p.m_player2 = action.m_player2;

    return p;
  }

  const uint64_t prepareState(uint8_t byteSize) const {
    uint64_t byteMask =
        byteSize == 8 ? -((uint64_t)1) : (1ull << (byteSize * 8ull)) - 1ull;
#if USE_DELTA_DIFFERENCES
    return byteMask & ((m_delta << 5) | (m_difference << 4) |
                       (static_cast<uint8_t>(m_button) << 2) |
                       (m_player2 << 1) | m_holding);
#else
    return byteMask & ((m_delta << 4) | (static_cast<uint8_t>(m_button) << 2) |
                       (m_player2 << 1) | m_holding);
#endif
  }
};

class Section {
public:
  enum class Identifier : uint8_t {
    /**
     * 00 XX    XXXX
     * -- --    ----
     * ID Size Count (2^X)
     */
    Input = 0,
    /**
     * 01 XX   XXXX   XXXXX   XXX
     * -- --   ----   -----   ---
     * ID Size Count  Repeats Reserved
     */
    Repeat,
    /**
     * 10 XXXX XX
     * -- ---- --
     * ID Type Size
     */
    Special,
  };

  enum class SpecialType : uint8_t { Restart = 0, RestartFull, Death, TPS };

private:
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
  Identifier m_id;
  uint8_t m_deltaSize;
  bool m_markedForRemoval = false;

  uint64_t getInputCountDirty() const { return m_playerInputs.size(); }
  uint64_t getRealDeltaSize() const {
    assert(m_deltaSize <= 3);

    return 1ull << (uint64_t)m_deltaSize;
  }
  uint64_t getInputCount() const { return 1ull << (uint64_t)m_countExp; }
  uint64_t getRepeatCount() { return 1ull << (uint64_t)m_repeatsExp; }
  inline const bool isSpecial() const { return m_id == Identifier::Special; }

  void copyFrom(Section &other) {
    assert(!isSpecial());

    m_playerInputs.insert(m_playerInputs.end(), other.m_playerInputs.begin(),
                          other.m_playerInputs.end());
  }

  size_t totalSize() {
    return newSizeAssumingDeltaSize(getInputCount(), getRealDeltaSize());
  }
  size_t newSizeAssumingDeltaSize(uint64_t count, uint64_t size) {
    switch (m_id) {
    case Identifier::Input: {
      return count * size + 1;
    }
    case Identifier::Repeat: {
      return count * size * getRepeatCount() + 2;
    }
    case Identifier::Special: {
      return 1 + 8 + size;
    }
    }

    return 0; // unreachable
  }

  static Section player(const std::span<const Action> actions, size_t start,
                        size_t end) {
    Section s;

    s.m_id = Identifier::Input;
    uint32_t count = 0;
    uint32_t swifts = 0;

    for (int i = start; i < end; i++) {
      auto &action = actions[i];
      if (action.m_holding || !action.swift()) {
        // #ifdef SLC_INSPECT
        //         std::println("Processing {}, marked swift {}", i,
        //         action.swift());
        // #endif

        s.m_playerInputs.push_back(PlayerInput::fromAction(
            actions[i], actions[i].delta() - actions[i - 1].delta()));
        count++;
      } else {
        swifts++;
      }
    }

    s.m_countExp = util::exponentOfTwo(count);

#ifdef SLC_INSPECT
    std::println(
        "Preparing Input section from {} to {}, with count {}, swifts {}",
        start, end, count, swifts);
#endif

    return s;
  }

  static Section player(const Action &start) {
    assert(start.isPlayer());
    Section s;

    s.m_id = Identifier::Input;
    s.m_playerInputs = {PlayerInput::fromAction(start, 0)};

    return s;
  }

  // void addPlayerInput(const Action &action) {
  //   m_playerInputs.push_back(PlayerInput::fromAction(action));
  // }

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
    s.m_deltaSize = action.getMinimumSize(0);

    return s;
  }

  const void write(std::ostream &s) const {
    if (m_markedForRemoval)
      return;

    switch (m_id) {
    case Identifier::Input: {
#ifdef SLC_INSPECT
      std::println("Writing Input section: {} count ({} exp), {} delta size",
                   getInputCount(), m_countExp, m_deltaSize);
#endif
      uint8_t header = m_countExp | (m_deltaSize << 4);

      util::binWrite(s, header);

      uint64_t byteSize = getRealDeltaSize();

      for (const auto &input : m_playerInputs) {
        uint64_t state = input.prepareState(byteSize);

        s.write(
            reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(&state)),
            byteSize);
      }

      break;
    }
    case Identifier::Repeat: {
      uint16_t header = static_cast<uint16_t>(Identifier::Repeat) << 14 |
                        m_deltaSize << 12 | m_countExp << 8 | m_repeatsExp << 3;

      util::binWrite(s, header);
      break;
    }
    case Identifier::Special: {
      uint8_t header = static_cast<uint8_t>(Identifier::Special) << 6 |
                       static_cast<uint8_t>(m_specialType) << 2 | m_deltaSize;

      util::binWrite(s, header);

      uint64_t delta = m_special.delta();

      s.write(
          reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(&delta)),
          getRealDeltaSize());

      switch (m_specialType) {
      case SpecialType::Restart:
      case SpecialType::RestartFull:
      case SpecialType::Death:
        util::binWrite(s, m_seed);
        break;
      case SpecialType::TPS:
        util::binWrite(s, m_tps);
        break;
      }

      break;
    }
    }
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

public:
  std::vector<Action> m_actions;

private:
  // "literally slc2"
  void prepareSections(std::vector<Section> &sections) {
    int i = 0;
    while (i < m_actions.size()) {
      if (!m_actions[i].isPlayer()) {
        sections.push_back(Section::special(m_actions[i]));
        continue;
      }

      uint64_t dd = 0;
      uint32_t count = 1;
      uint32_t pureCount = 1;
      uint32_t swifts = 0;
      uint32_t pureSwifts = 0;
      size_t start = i;
#if USE_DELTA_DIFFERENCES
      if (i > 1 && m_actions[i].delta() >= m_actions[i - 1].delta() &&
          m_actions[i].delta() - m_actions[i - 1].delta() <
              m_actions[i].delta()) {
        m_actions[i].m_difference = true;
        dd = m_actions[i].delta() - m_actions[i - 1].delta();
      }
#endif

      uint8_t minSize = m_actions[i].getMinimumSize(dd);

      while (i < m_actions.size() && pureCount < (1 << 16) &&
             m_actions[i].isPlayer() &&
             m_actions[i].getMinimumSize(dd) == minSize) {
        i++;
        count++;

        if (m_actions[i].delta() == 0 && !m_actions[i].m_holding &&
            m_actions[i - 1].m_holding != m_actions[i].m_holding &&
            m_actions[i - 1].m_player2 == m_actions[i].m_player2 &&
            m_actions[i - 1].m_type == m_actions[i].m_type) {
          m_actions[i - 1].m_swift = true;
          m_actions[i].m_swift = true;
          swifts++;

        } else {
          pureCount++;
        }

#if USE_DELTA_DIFFERENCES
        if (i > 1 && m_actions[i].delta() >= m_actions[i - 1].delta() &&
            m_actions[i].delta() - m_actions[i - 1].delta() <
                m_actions[i].delta()) {
          m_actions[i].m_difference = true;
          dd = m_actions[i].delta() - m_actions[i - 1].delta();
        }
#endif

        if (util::largestPowerOfTwo(pureCount) == pureCount) {
          pureSwifts = swifts;
        }
      }

      count--;

      count = util::largestPowerOfTwo(pureCount);
      i = start + count + pureSwifts;

      Section s = Section::player(m_actions, start, i);
      s.m_deltaSize = minSize;

      sections.push_back(s);
    }
  }

public:
  static constexpr size_t HEADER_SIZE = 4;
  static constexpr char HEADER[HEADER_SIZE] = {'S', 'L', 'C', '3'};

  void write(std::ostream &out) {
    out.write(HEADER, HEADER_SIZE);

    uint64_t metaSize = sizeof(Metadata);
    util::binWrite(out, metaSize);
    util::binWrite(out, m_meta);

    uint64_t actionCount = m_actions.size();
    util::binWrite(out, actionCount);

    std::vector<Section> sections;

    sections.reserve(
        actionCount); // worst case scenario; no optimizations will be applied

    prepareSections(sections); // Necessary for the replay to save; organizes
                               // actions into sections

    for (auto &section : sections) {
      section.write(out);
    }
  }
};

}; // namespace v3

SLC_NS_END

#endif // SLC_FORMATS_V3_HPP
