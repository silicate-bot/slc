#ifndef _SLC_V3_SECTION_HPP
#define _SLC_V3_SECTION_HPP

#include "slc/formats/v3/action.hpp"
#include "slc/formats/v3/atom.hpp"
#include "slc/util.hpp"

#include <cassert>
#include <deque>
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

  static PlayerInput fromAction(const Action &action) {
    assert(action.isPlayer());

    PlayerInput p;
    p.m_button = static_cast<Button>(action.m_type);
    if (action.swift()) {
      p.m_button = Button::Swift;
    }
    p.m_frame = action.m_frame;
    p.m_delta = action.delta();
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

  uint64_t prepareState(uint8_t byteSize) const {
    uint64_t byteMask =
        byteSize == 8 ? -((uint64_t)1) : (1ull << (byteSize * 8ull)) - 1ull;
    return byteMask & ((m_delta << 4) | (static_cast<uint8_t>(m_button) << 2) |
                       (m_player2 << 1) | m_holding);
  }

  bool weakEq(const PlayerInput &other) {
    return m_delta == other.m_delta && m_holding == other.m_holding &&
           m_player2 == other.m_player2 && m_button == other.m_button;
  }
};

class Section {
public:
  enum class Identifier : uint8_t {
    /**
     * 00 XX    XXXX       XXXXXXXX
     * -- --    ----       ---------
     * ID Size Count (2^X) Reserved
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
  uint16_t m_countExp;
  uint16_t m_repeatsExp;

  // Special
  SpecialType m_specialType;
  uint64_t m_seed;
  double m_tps;
  Action m_special;

protected:
public:
  Identifier m_id;
  uint16_t m_deltaSize;
  std::vector<PlayerInput> m_playerInputs;
  bool m_markedForRemoval = false;

  uint64_t getInputCountDirty() const { return m_playerInputs.size(); }
  uint64_t getRealDeltaSize() const {
    assert(m_deltaSize <= 3);

    return 1ull << (uint64_t)m_deltaSize;
  }
  uint64_t getInputCount() const { return 1ull << (uint64_t)m_countExp; }
  uint64_t getRepeatCount() { return 1ull << (uint64_t)m_repeatsExp; }
  inline bool isSpecial() const { return m_id == Identifier::Special; }

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

    for (size_t i = start; i < end; i++) {
      auto &action = actions[i];
      if (action.m_holding || !action.swift()) {
#ifdef SLC_INSPECT
        std::println("Processing {} button {} delta {}, marked swift {}", i,
                     static_cast<int>(action.m_type), action.delta(),
                     action.swift());
#endif

        s.m_playerInputs.push_back(PlayerInput::fromAction(actions[i]));
        count++;
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
    s.m_playerInputs = {PlayerInput::fromAction(start)};

    return s;
  }

  static Result<Section> special(const Action &action) {
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
      return std::unexpected("Invalid action to create a special section - "
                             "somehow got past assertion");
    }

    s.m_special = action;
    s.m_deltaSize = action.getMinimumSize();

    return s;
  }

  std::vector<Section> runLengthEncode() {
    assert(m_id == Identifier::Input);

    std::deque<PlayerInput> queue;
    std::vector<Section> newSections;
    std::vector<PlayerInput> freeInputs;

    constexpr size_t MAX_CLUSTER_SIZE = 16;

    size_t i = 0;
    while (i < m_playerInputs.size()) {
      int64_t bestClusterScore = 0;
      size_t bestClusterTotal = 1;
      size_t bestCluster = 0;

      for (size_t cluster = 1; cluster <= MAX_CLUSTER_SIZE; cluster *= 2) {
        if ((i + (cluster * 2) - 1) >= m_playerInputs.size())
          break;
        bool matching = true;

        size_t j = 1;
        while ((i + cluster * j) < m_playerInputs.size() && j < (1 << 16)) {
          for (size_t k = i; k < i + cluster; k++) {
            if (!m_playerInputs[k].weakEq(m_playerInputs[k + (cluster * j)])) {
              matching = false;
              break;
            }
          }

          if (!matching)
            break;
          j++;
        }
        // j--;

        j = util::largestPowerOfTwo(j);
        size_t total = j * cluster;
        int64_t score = (int64_t)total - (int64_t)cluster;

        if (bestClusterScore <= score) {
          // std::println("cluster {}, score {}, total {}, j {}", cluster,
          // score,
          //                       total, j);
          bestCluster = cluster;
          bestClusterScore = score;
          bestClusterTotal = total;
        }
      }

      if (bestCluster > 0) {
        size_t j = 0;
        while (j < freeInputs.size()) {
          uint64_t count = freeInputs.size() - j;
          Section s;
          s.m_playerInputs = std::vector<PlayerInput>(
              freeInputs.begin() + j, freeInputs.begin() + j + count);
          s.m_countExp = util::exponentOfTwo(count);
          s.m_deltaSize = m_deltaSize;
          s.m_id = Identifier::Input;

          j += util::largestPowerOfTwo(count);
          newSections.push_back(s);
        }

        freeInputs.clear();

        Section s;
        s.m_id = Identifier::Repeat;
        s.m_countExp = util::exponentOfTwo(bestCluster);
        s.m_repeatsExp = util::exponentOfTwo(bestClusterTotal / bestCluster);
        s.m_deltaSize = m_deltaSize;
        for (size_t k = 0; k < bestCluster; k++) {
          s.m_playerInputs.push_back(m_playerInputs[i + k]);
        }

        newSections.push_back(s);
      } else {
        freeInputs.push_back(m_playerInputs[i]);
        bestClusterTotal = 1;
      }

      i += bestClusterTotal;
    }

    size_t j = 0;
    while (j < freeInputs.size()) {
      uint64_t count = util::largestPowerOfTwo(freeInputs.size() - j);
      Section s;
      s.m_playerInputs = std::vector<PlayerInput>(
          freeInputs.begin() + j, freeInputs.begin() + j + count);
      s.m_countExp = util::exponentOfTwo(count);
      s.m_deltaSize = m_deltaSize;
      s.m_id = Identifier::Input;

      j += count;
      newSections.push_back(s);
    }

    return newSections;
  }

  static void read(std::istream &s, std::vector<Action> &actions) {
    uint16_t initialHeader = util::binRead<uint16_t>(s);

    Identifier id = static_cast<Identifier>(initialHeader >> 14);
    switch (id) {
    case Identifier::Input: {
      uint16_t deltaSize = (initialHeader >> 12) & 0b11;
      uint16_t countExp = (initialHeader >> 8) & 0b1111;

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
    case Identifier::Repeat: {
      uint16_t deltaSize = (initialHeader >> 12) & 0b11;
      uint16_t countExp = (initialHeader >> 8) & 0b1111;
      uint16_t repeatsExp = (initialHeader >> 3) & 0b11111;

      uint64_t byteSize = 1ull << (uint64_t)deltaSize;
      uint64_t length = 1ull << (uint64_t)countExp;
      uint64_t repeats = 1ull << (uint64_t)repeatsExp;

      std::vector<PlayerInput> inputs;

      for (uint64_t i = 0; i < length; i++) {
        uint64_t state = 0;
        s.read(reinterpret_cast<char *>(&state), byteSize);

        uint64_t previousFrame = 0;
        if (inputs.size() > 0) {
          previousFrame = inputs.back().m_frame;
        }

        PlayerInput p = PlayerInput::fromState(previousFrame, state);
        inputs.push_back(p);
      }

      for (uint64_t i = 0; i < repeats; i++) {
        for (size_t j = 0; j < inputs.size(); j++) {
          auto &p = inputs[j];
          uint64_t previousFrame = 0;
          if (actions.size() > 0) {
            previousFrame = actions.back().m_frame;
          }

          if (p.m_button == PlayerInput::Button::Swift) {
            actions.push_back(Action(previousFrame, p.m_delta,
                                     Action::ActionType::Jump, true,
                                     p.m_player2));
            actions.back().m_swift = true;
            actions.push_back(Action(previousFrame + p.m_delta, 0,
                                     Action::ActionType::Jump, false,
                                     p.m_player2));
            actions.back().m_swift = true;
          } else {
            actions.push_back(
                Action(previousFrame, p.m_delta,
                       static_cast<Action::ActionType>(p.m_button), p.m_holding,
                       p.m_player2));
          }
        }
      }

      break;
    }
    case Identifier::Special: {
      uint16_t deltaSize = (initialHeader >> 8) & 0b11;
      SpecialType specialType =
          static_cast<SpecialType>((initialHeader >> 10) & 0b1111);

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
    }
  }

  void write(std::ostream &s) const {
    if (m_markedForRemoval)
      return;

    switch (m_id) {
    case Identifier::Input: {
#ifdef SLC_INSPECT
      std::println("Writing Input section: {} count ({} exp), {} delta size",
                   getInputCount(), m_countExp, m_deltaSize);
#endif
      uint16_t header = (m_countExp << 8) | (m_deltaSize << 12);

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

      uint64_t byteSize = getRealDeltaSize();

      for (const auto &input : m_playerInputs) {

        uint64_t state = input.prepareState(byteSize);

        s.write(
            reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(&state)),
            byteSize);
      }

      break;
    }
    case Identifier::Special: {
      uint16_t header = static_cast<uint16_t>(Identifier::Special) << 14 |
                        static_cast<uint16_t>(m_specialType) << 10 |
                        (m_deltaSize << 8);

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
} // namespace v3

SLC_NS_END

#endif
