#ifndef SLC_HPP
#define SLC_HPP

#include <expected>
#include <iostream>
#include <vector>

namespace slc {
namespace _util {
template <typename T> T binRead(std::istream &s) {
  T temp;

  s.read(reinterpret_cast<char *>(&temp), sizeof(T));

  return temp;
}

template <typename T> void binWrite(std::ostream &s, const T &val) {
  s.write(reinterpret_cast<const char *>(&val), sizeof(T));
}
} // namespace _util

/**
 * A replay input.
 *
 * Internally, in memory, it's represented as one 64-bit value.
 * If it's a TPS changing input then type casting is used from uint64_t to
 * float.
 */
class Input {
public:
  /**
   * A replay input type.
   */
  enum class InputType : uint8_t {
    // No action is associated with this type.
    Skip = 0,
    // Perform a jump (in-game button 1)
    Jump = 1,
    // Perform left movement (in-game button 2)
    Left = 2,
    // Perform right movement (in-game button 3)
    Right = 3,
    // Restart the level, possibly from the latest checkpoint
    Restart = 4,
    // Restart the level, removing all prior checkpoints
    RestartFull = 5,
    // No action is associated with this type. Acknowledges that the player
    // should die on this frame or later.
    Death = 6,
    // Change the TPS of the macro.
    TPS = 7
  };

private:
  friend class _Blob;

  // This is only 64bit in memory itself; it's okay if a replay takes up a bit
  // of RAM, but it's not okay if it takes up disk space.
  //
  // Replay inputs will be saved to disk based on their parent blob's byte size.
  uint64_t m_state;


public:
  // The TPS that is to be set.
  // This is only set when the input type is TPS.
  double m_tps = 0.0;

  // These are helper fields that are actually included in the state.
  // They're here for ease of use.

  // The frame of the input. This automatically gets converted to a delta when
  // saving.
  uint64_t m_frame = 0;

  // Whether the input is for player 2.
  bool m_player2 = false;

  // The button associated with the input.
  InputType m_button = InputType::Skip;

  // Whether the input is a hold or release.
  bool m_holding = false;

  Input() : m_state(0) {}
  Input(uint64_t currentFrame, uint64_t delta, InputType type, bool p2, bool hold) {
    m_state =
        (delta << 5) | (static_cast<uint8_t>(type) << 2) | (p2 << 1) | hold;

    m_frame = currentFrame + delta;
    m_player2 = p2;
    m_button = type;
    m_holding = hold;
  }

  Input(uint64_t currentFrame, uint64_t delta, float tps) {
    m_state = (delta << 5) | (static_cast<uint8_t>(InputType::TPS) << 2);

    m_frame = currentFrame + delta;
    m_tps = tps;
    m_button = InputType::TPS;
  }

  inline const uint8_t requiredBytes() const {
    if (m_button == InputType::TPS)
      return 8;

    if (m_state < 0x100) {
      return 1;
    } else if (m_state < 0x10000) {
      return 2;
    } else if (m_state < 0x100000000) {
      return 4;
    } else {
      return 8;
    }
  }

  void updateHelpers(uint64_t currentFrame) {
    m_frame = currentFrame + (m_state >> 5);
    m_player2 = (m_state & 2) >> 1;
    m_button = static_cast<InputType>((m_state & 0b11100) >> 2);
    m_holding = m_state & 1;
  }
};

class _Blob {
public:
  // How many bytes one input in the blob takes up.
  // The maximum value for this is 8.
  uint64_t m_byteSize = 1;
  // An index to the start of the blob in the inputs vector.
  uint64_t m_start;
  // How long the blob is in the inputs vector.
  uint64_t m_length;

public:
  _Blob() = default;
  _Blob(uint64_t size, uint64_t start)
      : m_byteSize(size), m_start(start), m_length(1) {}

  static _Blob readFromMeta(std::istream &s) {
    _Blob b;

    b.m_byteSize = _util::binRead<uint64_t>(s);
    b.m_start = _util::binRead<uint64_t>(s);
    b.m_length = _util::binRead<uint64_t>(s);

    return b;
  }

  const void writeMeta(std::ostream &s) const {
    if (m_length <= 0)
      return;

    _util::binWrite(s, m_byteSize);
    _util::binWrite(s, m_start);
    _util::binWrite(s, m_length);
  }

  const void write(std::ostream &s, const std::vector<Input> &inputs) const {
    if (m_length <= 0)
      return;

    uint64_t byteMask =
        m_byteSize == 8 ? -((uint64_t)1) : (1ull << (m_byteSize * 8ull)) - 1ull;


    for (int i = m_start; i < m_start + m_length; i++) {
      uint64_t state = inputs.at(i).m_state & byteMask;

      s.write(
          reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(&state)),
          m_byteSize);

      if (inputs.at(i).m_button == Input::InputType::TPS) {
        _util::binWrite(s, inputs.at(i).m_tps);
      }
    }
  }

  void read(std::istream &s, std::vector<Input> &inputs, uint64_t& frame) {
    for (int i = m_start; i < m_start + m_length; i++) {
      s.read(reinterpret_cast<char *>(
                 reinterpret_cast<uintptr_t>(&inputs.at(i).m_state)),
             m_byteSize);
            
      inputs.at(i).updateHelpers(frame);
      frame = inputs.at(i).m_frame;

      if (inputs.at(i).m_button == Input::InputType::TPS) {
        inputs.at(i).m_tps = _util::binRead<double>(s);
      }
    }
  }
};

/**
 * An slc replay.
 *
 * When saved, replay format consists of a series of blobs, comprised of groups
of inputs.
 * Blobs can have their own byte size (declared in the blobs section of the
file).
 * When saving the replay automatically determines how to split its replay into
blobs to maximize
 * performance vs filesize.
 *
 * When it's used, blobs are cast away in favor of one continuous vector of
inputs;
 * this is due to the unwieldiness of using blobs in actual, real world bots.
 * Inputs are stored using frame deltas; differences in frame from the previous
input.
 * This allows blobs to consistently be of smaller size.
 */
template <typename M = void> class Replay {
private:
  static constexpr char HEADER[] = "SILL";
  static constexpr char FOOTER[] = "EOM";

  using Meta = M;
  using Self = Replay<Meta>;

  enum class ReplayError {
    OpenFileError,
  };

  // It's much faster to do one lookup rather than two lookups for
  // input retrieval during runtime; therefore blobs may only
  // index into the inputs vector. There's no need for each of them to
  // have their own vector.
  //
  // Plus, you only need to save the input index in your bot rather than
  // the blob index + the input index in the blob.
  //
  // Blobs aren't really a thing at runtime, only when saving/loading,
  // therefore there's no real need to keep track of them (writing inputs to the
  // vector would become more expensive than it needs to be).
  //
  // It's okay if writes are a little longer than reads; when saving a macro
  // the important bit is that it *saves correctly*.

private:
  std::vector<Input> m_inputs;

public:
  double m_tps = 240.0;
  Meta m_meta;

  void addInput(const uint64_t frame, const Input::InputType type,
                const bool p2, const bool hold) {
    uint64_t currentFrame = m_inputs.empty() ? 0 : m_inputs.back().m_frame;

    if (frame < currentFrame) {
      throw std::runtime_error("Frame is less than the current frame.");
    }

    if (type == Input::InputType::TPS) {
      throw std::runtime_error("TPS inputs must be added with addTPSInput.");
    }

    m_inputs.emplace_back(currentFrame, frame - currentFrame, type, p2, hold);
  }

  void addTPSInput(const uint64_t frame, const float tps) {
    uint64_t currentFrame = m_inputs.empty() ? 0 : m_inputs.back().m_frame;

    if (frame < currentFrame) {
      throw std::runtime_error("Frame is less than the current frame.");
    }

    m_inputs.emplace_back(currentFrame, frame - currentFrame, tps);
  }

  void popInput() { m_inputs.pop_back(); }
  void clearInputs() { m_inputs.clear(); }

  void pruneAfterFrame(const uint64_t frame) {
    m_inputs.erase(std::remove_if(m_inputs.begin(), m_inputs.end(),
                                  [frame](const Input &input)
                                  {
                                    return input.m_frame >= frame;
                                  }),
                   m_inputs.end());
  }

  const std::vector<Input> &getInputs() const { return m_inputs; }

  [[nodiscard]]
  static std::expected<Self, ReplayError> read(std::istream &s) {
    Self replay;
    char header[4];
    s.read(header, sizeof(header));
    if (std::memcmp(header, HEADER, sizeof(HEADER)) != 0) {
      return std::unexpected(ReplayError::OpenFileError);
    }

    replay.m_tps = _util::binRead<double>(s);
    uint64_t metaSize = _util::binRead<uint64_t>(s);
    if (metaSize != sizeof(Meta)) {
      return std::unexpected(ReplayError::OpenFileError);
    }

    replay.m_meta = _util::binRead<Meta>(s);

    uint64_t length = _util::binRead<uint64_t>(s);
    replay.m_inputs.resize(length);

    uint64_t blobCount = _util::binRead<uint64_t>(s);

    std::vector<_Blob> blobs(blobCount);
    for (auto &blob : blobs) {
      blob = _Blob::readFromMeta(s);
    }

    uint64_t frame = 0;

    for (int i = 0; i < blobCount; i++) {
      blobs.at(i).read(s, replay.m_inputs, frame);
    }

    char footer[3];
    s.read(footer, sizeof(footer));
    if (std::memcmp(footer, FOOTER, sizeof(FOOTER)) != 0) {
      return std::unexpected(ReplayError::OpenFileError);
    }

    return replay;
  }

  /**
   * Save a replay to a stream.
   * Empty replays are supported.
   */
  void write(std::ostream &s) {
    s.write(HEADER, 4);
    _util::binWrite(s, m_tps);
    _util::binWrite(s, static_cast<uint64_t>(sizeof(Meta)));
    _util::binWrite(s, m_meta);
    _util::binWrite(s, static_cast<uint64_t>(m_inputs.size()));

    // Can't preallocate memory here unfortunately; since blobs
    // are highly dynamic and one input can change the blob count, this
    // vector unfortunately has to be allocated along with elements being added.
    //
    // What's very unfun with this system is that, in theory, a perfectly
    // crafted macro with size-1, size-2, size-4 and size-8 inputs consecutively
    // will take up more space than a typical macro. This scenario is so
    // incredibly unlikely, that it's worth the tradeoff.
    std::vector<_Blob> blobs;
    uint64_t currentBlobSize = 0;
    for (size_t i = 0; i < m_inputs.size(); i++) {
      const auto &input = m_inputs.at(i);
      uint8_t inputSize = input.requiredBytes();

      if (blobs.empty()) {
        blobs.push_back(_Blob(inputSize, i));
        continue;
      }

      auto &blob = blobs.at(blobs.size() - 1);

      if (blob.m_byteSize < inputSize) {
        blobs.push_back(_Blob(inputSize, i));
        continue;
      } else if (blob.m_byteSize > inputSize) {
        // Adding a blob is more expensive than adding a new 64bit input.
        // We naively add blobs here to later clean them up in the second pass.

        blobs.push_back(_Blob(inputSize, i));
        continue;
      } else {
        blob.m_length++;
      }
    }

    size_t zeroSizedBlobs = 0;

    // Second pass; consolidates blobs into prior blobs that are too small
    // (a new blob for one 1-byte input is more expensive than adding that input
    // to an 8-byte blob)
    for (size_t i = blobs.size() - 1; i > 0; i--) {
      uint64_t blobSize = blobs[i].m_byteSize * blobs[i].m_length;

      if (blobSize < sizeof(_Blob)) {
        // Blobs with length 0 are empty blobs, and won't be written to the
        // file.
        if (blobs[i].m_byteSize > blobs[i - 1].m_byteSize &&
            blobs[i - 1].m_byteSize * blobs[i].m_length < sizeof(_Blob)) {
          blobs[i - 1].m_length += blobs[i].m_length;
          blobs[i - 1].m_byteSize = blobs[i].m_byteSize;
          blobs[i].m_length = 0;
          zeroSizedBlobs++;
        } else if (blobs[i].m_byteSize < blobs[i - 1].m_byteSize &&
          blobs[i - 1].m_byteSize * blobs[i].m_length < sizeof(_Blob)
        ) {
          blobs[i - 1].m_length += blobs[i].m_length;
          blobs[i].m_length = 0;
          zeroSizedBlobs++;
        }
      }
    }

    _util::binWrite(s, static_cast<uint64_t>(blobs.size() - zeroSizedBlobs));

    // horrible for the economy
    for (const auto &blob : blobs) {
      blob.writeMeta(s);
    }

    for (const auto &blob : blobs) {
      blob.write(s, m_inputs);
    }

    s.write(FOOTER, 3);
  }
};

} // namespace slc

#endif // SLC_HPP
