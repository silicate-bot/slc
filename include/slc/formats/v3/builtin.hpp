#ifndef _SLC_V3_BUILTIN_HPP
#define _SLC_V3_BUILTIN_HPP

#include "slc/formats/v3/atom.hpp"
#include "slc/formats/v3/section.hpp"
#include "slc/util.hpp"

SLC_NS_BEGIN

namespace v3 {

struct ActionAtom {
  static inline constexpr AtomId id = AtomId::Action;
  size_t size;

  std::vector<Action> m_actions;

private:
  static inline bool swiftCompatible(std::vector<Action> &actions, size_t i) {
    assert(i < actions.size());

    return actions[i].delta() == 0 && !actions[i].m_holding &&
           actions[i - 1].m_holding != actions[i].m_holding &&
           actions[i - 1].m_player2 == actions[i].m_player2 &&
           actions[i - 1].m_type == actions[i].m_type &&
           actions[i].m_type == Action::ActionType::Jump;
  }

  static inline bool canJoin(std::vector<Action> &actions, size_t count,
                             size_t i) {
    static constexpr size_t MAX_SECTION_ACTIONS = 1 << 16;

    return i < (actions.size() - 1) && count < MAX_SECTION_ACTIONS &&
           actions[i + 1].isPlayer() &&
           actions[i + 1].getMinimumSize() == actions[i].getMinimumSize();
  }

  // "literally slc2"
  static Result<> prepareSections(std::vector<Action> &actions,
                                  std::vector<Section> &sections) {
    size_t i = 0;
    while (i < actions.size()) {
      if (!actions[i].isPlayer()) {
        auto section = TRY(Section::special(actions[i]));

        sections.push_back(section);

        continue;
      }

      uint32_t count = 1;
      uint32_t pureCount = 1;
      uint32_t swifts = 0;
      uint32_t pureSwifts = 0;
      size_t start = i;

      uint8_t minSize = actions[i].getMinimumSize();

      while (canJoin(actions, pureCount, i)) {
        i++;
        count++;

        if (swiftCompatible(actions, i)) {
          actions[i - 1].m_swift = true;
          actions[i].m_swift = true;
          swifts++;
        } else {
          pureCount++;
        }

        if ((uint32_t)util::largestPowerOfTwo(pureCount) == pureCount) {
          pureSwifts = swifts;
        }
      }

      count--;

      count = util::largestPowerOfTwo(pureCount);
      i = start + count + pureSwifts;

      Section s = Section::player(actions, start, i);
      s.m_deltaSize = minSize;

      auto realSections = s.runLengthEncode();

      sections.insert(sections.end(), realSections.begin(), realSections.end());
    }

    return {};
  }

public:
  /**
   * Read an action atom from a stream of given size.
   * It's recommended to use this function from an atom registry.
   * See [`AtomRegistry::readAll`].
   */
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

  /**
   * Write an action atom to a stream.
   * It's recommended to use this function from an atom registry.
   * See [`AtomRegistry::writeAll`].
   */
  Result<> write(std::ostream &out) {
    util::binWrite<uint64_t>(out, m_actions.size());

    std::vector<Section> sections;

    TRY(ActionAtom::prepareSections(m_actions, sections));

    for (auto &section : sections) {
      section.write(out);
    }

    return {};
  }

  /**
   * Add a player action to a replay.
   * This only supports Jump, Left and Right actions.
   * Frame delta is based on previous action.
   */
  Result<> addAction(uint64_t frame, Action::ActionType actionType,
                     bool holding, bool p2) {
    using At = Action::ActionType;
    if (!util::inRange<At, At::Jump, At::Right>(actionType)) {
      return std::unexpected("invalid action type provided to addAction "
                             "function; use other overloads");
    }

    uint64_t previousFrame = 0;
    if (m_actions.size() > 0) {
      previousFrame = m_actions.back().m_frame;
    }

    uint64_t delta = frame - previousFrame;

    m_actions.push_back(Action(previousFrame, delta, actionType, holding, p2));

    return {};
  }

  /**
   * Add a death action to a replay.
   * This only supports Restart, RestartFull and Death actions.
   * Frame delta is based on previous action.
   */
  Result<> addAction(uint64_t frame, Action::ActionType actionType,
                     uint64_t seed) {
    using At = Action::ActionType;
    if (!util::inRange<At, At::Restart, At::Death>(actionType)) {
      return std::unexpected("invalid action type provided to addAction "
                             "function; use other overloads");
    }

    uint64_t previousFrame = 0;
    if (m_actions.size() > 0) {
      previousFrame = m_actions.back().m_frame;
    }

    uint64_t delta = frame - previousFrame;

    m_actions.push_back(Action(previousFrame, delta, actionType, seed));

    return {};
  }

  /**
   * Add a TPS action to a replay.
   * Frame delta is based on previous action.
   */
  Result<> addAction(uint64_t frame, double tps) {
    if (tps <= 0.0) {
      return std::unexpected(
          "invalid tps provided to addAction function; must be positive");
    }

    uint64_t previousFrame = 0;
    if (m_actions.size() > 0) {
      previousFrame = m_actions.back().m_frame;
    }

    uint64_t delta = frame - previousFrame;

    m_actions.push_back(Action(previousFrame, delta, tps));

    return {};
  }

  /**
   * Clips the actions up to a specific frame, removing those that happened
   * after or during the given frame.
   */
  void clipActions(uint64_t frame) {
    auto _ = std::remove_if(m_actions.begin(), m_actions.end(),
                            [frame](auto &a) { return a.m_frame >= frame; });
  }
};

} // namespace v3

SLC_NS_END

#endif
