#ifndef _SLC_V3_SECTION_HPP
#define _SLC_V3_SECTION_HPP

#include "slc/formats/v3/action.hpp"
#include "slc/formats/v3/atom.hpp"
#include "slc/util.hpp"

#include <cassert>
#include <print>
#include <vector>

SLC_NS_BEGIN

namespace v3 {

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

  static PlayerInput fromState(uint64_t prevFrame, uint64_t state) {
    PlayerInput p;
    p.m_delta = state >> 4;
    p.m_frame = prevFrame + p.m_delta;

    uint8_t button = (state >> 2) & 0b11;
    assert(button <= 3);

    p.m_button = static_cast<Button>(button);
    p.m_holding = (state & 0b1) == 0b1;
    p.m_player2 = (state & 0b10) == 0b10;

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
  std::vector<PlayerInput> m_playerInputs;
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
#ifdef SLC_INSPECT
        std::println("Processing {} button {} delta {}, marked swift {}", i,
                     static_cast<int>(action.m_type), action.delta(),
                     action.swift());
#endif

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

  static void read(std::istream &s, std::vector<Action> &actions) {
    uint8_t initialHeader = util::binRead<uint8_t>(s);

    Identifier id = static_cast<Identifier>(initialHeader >> 6);
    switch (id) {
    case Identifier::Input: {
      uint8_t deltaSize = (initialHeader >> 4) & 0b11;
      uint8_t countExp = initialHeader & 0b1111;

      uint64_t byteSize = 1ull << (uint64_t)deltaSize;
      uint64_t length = 1ull << (uint64_t)countExp;

      for (uint64_t i = 0; i < length; i++) {
        uint64_t state = 0;
        s.read(reinterpret_cast<char *>(&state), byteSize);

        uint64_t previousFrame = 0;
        if (actions.size() > 0) {
          previousFrame = actions.back().m_frame;
        }

        PlayerInput p = PlayerInput::fromState(previousFrame, state);

        if (p.m_button == PlayerInput::Button::Swift) {
          actions.push_back(Action(previousFrame, p.m_delta,
                                   Action::ActionType::Jump, true,
                                   p.m_player2));
          actions.back().m_swift = true;
          actions.push_back(Action(p.m_frame, 0, Action::ActionType::Jump,
                                   false, p.m_player2));
          actions.back().m_swift = true;
        } else {
          actions.push_back(Action(previousFrame, p.m_delta,
                                   static_cast<Action::ActionType>(p.m_button),
                                   p.m_holding, p.m_player2));
        }
      }

      break;
    };
    case Identifier::Special: {
      uint8_t deltaSize = initialHeader & 0b11;
      SpecialType specialType =
          static_cast<SpecialType>((initialHeader << 2) & 0b1111);

      uint64_t frameDelta;
      s.read(reinterpret_cast<char *>(&frameDelta), 1 << deltaSize);

      uint64_t currentFrame = 0;
      if (actions.size() > 0) {
        currentFrame = actions.back().m_frame;
      }

      switch (specialType) {
      case SpecialType::TPS: {
        double tps = util::binRead<double>(s);
        actions.push_back(Action(currentFrame, frameDelta, tps));
        break;
      }
      case SpecialType::Restart:
      case SpecialType::RestartFull:
      case SpecialType::Death: {
        uint64_t seed = util::binRead<uint64_t>(s);

        actions.push_back(Action(
            currentFrame, frameDelta,
            static_cast<Action::ActionType>(static_cast<int>(specialType) + 4),
            seed));
      }
      }

      break;
    };
    default: {
      break;
    }
    }
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

struct ActionAtom {
  static inline constexpr AtomId id = AtomId::Action;
  size_t size;

  std::vector<Action> m_actions;

  // "literally slc2"
  static void prepareSections(std::vector<Action> &actions,
                              std::vector<Section> &sections) {
    int i = 0;
    while (i < actions.size()) {
      if (!actions[i].isPlayer()) {
        sections.push_back(Section::special(actions[i]));
        continue;
      }

      uint64_t dd = 0;
      uint32_t count = 1;
      uint32_t pureCount = 1;
      uint32_t swifts = 0;
      uint32_t pureSwifts = 0;
      size_t start = i;
#if USE_DELTA_DIFFERENCES
      if (i > 1 && actions[i].delta() >= actions[i - 1].delta() &&
          actions[i].delta() - actions[i - 1].delta() < actions[i].delta()) {
        actions[i].m_difference = true;
        dd = actions[i].delta() - actions[i - 1].delta();
      }
#endif

      uint8_t minSize = actions[i].getMinimumSize(dd);

      while (i < (actions.size() - 1) && pureCount < (1 << 16) &&
             actions[i + 1].isPlayer() &&
             actions[i + 1].getMinimumSize(dd) == minSize) {
        i++;
        count++;

        if (actions[i].delta() == 0 && !actions[i].m_holding &&
            actions[i - 1].m_holding != actions[i].m_holding &&
            actions[i - 1].m_player2 == actions[i].m_player2 &&
            actions[i - 1].m_type == actions[i].m_type &&
            actions[i].m_type == Action::ActionType::Jump) {
          actions[i - 1].m_swift = true;
          actions[i].m_swift = true;
          swifts++;

        } else {
          pureCount++;
        }

#if USE_DELTA_DIFFERENCES
        if (i > 1 && actions[i].delta() >= actions[i - 1].delta() &&
            actions[i].delta() - actions[i - 1].delta() < actions[i].delta()) {
          actions[i].m_difference = true;
          dd = actions[i].delta() - actions[i - 1].delta();
        }
#endif

        if (util::largestPowerOfTwo(pureCount) == pureCount) {
          pureSwifts = swifts;
        }
      }

      count--;

      count = util::largestPowerOfTwo(pureCount);
      i = start + count + pureSwifts;

      Section s = Section::player(actions, start, i);
      s.m_deltaSize = minSize;

      sections.push_back(s);
    }
  }

  static Result<ActionAtom> read(std::istream &in, size_t size) {
    ActionAtom a;
    a.size = size;

    size_t count = util::binRead<uint64_t>(in);
    a.m_actions.reserve(count);

    while (a.m_actions.size() < count) {
      Section::read(in, a.m_actions);
    }

    return a;
  }

  Result<> write(std::ostream &out) {
    util::binWrite<uint64_t>(out, m_actions.size());

    std::vector<Section> sections;

    ActionAtom::prepareSections(m_actions, sections);

    for (auto &section : sections) {
      section.write(out);
    }

    return {};
  }

  void addAction(uint64_t frame, Action::ActionType actionType, bool holding,
                 bool p2) {
    uint64_t previousFrame = 0;
    if (m_actions.size() > 0) {
      previousFrame = m_actions.back().m_frame;
    }

    uint64_t delta = frame - previousFrame;

    m_actions.push_back(Action(previousFrame, delta, actionType, holding, p2));
  }
};

} // namespace v3

SLC_NS_END

#endif
