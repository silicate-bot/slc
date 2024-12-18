#ifndef SLC_HPP
#define SLC_HPP

#include <type_traits>
#include <vector>
#include <iostream>
#include <memory>
#include <expected>

namespace slc {

class Input {
public:
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
        // No action is associated with this type. Acknowledges that the player should die on this frame or later.
        Death = 6,
        // Change the TPS of the macro.
        TPS = 7
    };

private:
    friend class _Blob;

    // This is only 64bit in memory itself; it's okay if a replay takes up a bit of RAM,
    // but it's not okay if it takes up disk space.
    //
    // Replay inputs will be saved to disk based on their parent blob's byte size.
    // Still, there's no reason to make the replay use more memory than necessary!
    uint64_t m_state;

public:
    inline const uint8_t requiredBytes() const {
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

    /**
     * Get the internal state of the input.
     */
    inline const uint64_t state() const {
        return m_state;
    }

    /**
     * Get the input type (button) of the input.
     */
    inline const InputType type() const {
        return static_cast<InputType>((m_state & 0x1C) >> 2);
    }

    /**
     * Get whether the input is a hold or release.
     * This value should be set only for input types 1-3.
     */
    inline const bool hold() const {
        return (m_state & 0x1) != 0;
    }

    /**
     * Get whether the input is for player 2.
     * This value should only be set for input types 1-3.
     */
    inline const bool player2() const {
        return (m_state & 0x2) != 0;
    }

    /**
     * Get the frame delta of the input.
     * This value should always be set.
     */
    inline const uint64_t delta() const {
        return (m_state ^ 0x1F) >> 5;
    }
};

class _Blob {
private:
    // How many bytes one input in the blob takes up.
    // The maximum value for this is 8.
    uint8_t m_byteSize = 1;
    // An index to the start of the blob in the inputs vector.
    size_t m_start;
    // How long the blob is in the inputs vector.
    size_t m_length;

public:
    void write(std::ostream& s, const std::vector<Input>& inputs) {
        uint64_t byteMask = m_byteSize == 8 ? -((uint64_t)1) : (1 << (m_byteSize * 8)) - 1;

        for (int i = m_start; i < m_start + m_length; i++) {
            uint64_t state = inputs.at(i).state() & byteMask;
            s.write(reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(&state) + (8 - m_byteSize)), m_byteSize);
        }
    }

    void read(std::istream& s, std::vector<Input>& inputs) {
        uint64_t byteMask = m_byteSize == 8 ? -((uint64_t)1) : (1 << (m_byteSize * 8)) - 1;

        for (int i = m_start; i < m_start + m_length; i++) {
            s.read(reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(&inputs.at(i).m_state) + (8 - m_byteSize)), m_byteSize);
        }
    }
};

template<typename M = void>
class Replay {
private:
    static constexpr char[] HEADER = "SILL";

    using Meta = M;
    using Self = Replay<Meta>;

    enum class ReplayError {
        OpenFileError,
    }

    // It's much faster to do one lookup rather than two lookups for
    // input retrieval during runtime; therefore blobs may only
    // index into the inputs vector. There's no need for each of them to
    // have their own vector.
    //
    // Plus, you only need to save the input index in your bot rather than
    // the blob index + the input index in the blob.
    //
    // Blobs aren't really a thing at runtime, only when saving/loading,
    // therefore there's no real need to keep track of them (writing inputs to the vector
    // would become more expensive than it needs to be).

    std::vector<Input> m_inputs;
    double m_tps;
    uint64_t m_seed;
    Meta m_meta;

public:
    [[nodiscard]]
    static std::expected<Self, ReplayError> read(std::istream& s) {
        Self replay;
        char header[4];
        s.read(header, sizeof(header));
        if (std::memcmp(header, HEADER, sizeof(HEADER)) != 0) {
            return std::make_unexpected(ReplayError::OpenFileError);
        }

        s.read(reinterpret_cast<char*>(&replay.m_tps), sizeof(replay.m_tps));
        s.read(reinterpret_cast<char*>(&replay.m_meta), sizeof(replay.m_meta));

        uint64_t length = 0;
        s.read(reinterpret_cast<char*>(&length), sizeof(length));
        replay.m_inputs.resize(length);

        uint64_t blobCount = 0;
        s.read(reinterpret_cast<char*>(&blobCount), sizeof(blobCount));
        std::vector<_Blob> blobs(blobCount);
        for (int i = 0; i < blobCount; i++) {
            // TODO: rewrite this
            s.read(reinterpret_cast<char*>(&blobs.at(i).m_byteSize), sizeof(blobs.at(i).m_byteSize));
            s.read(reinterpret_cast<char*>(&blobs.at(i).m_start), sizeof(blobs.at(i).m_start));
            s.read(reinterpret_cast<char*>(&blobs.at(i).m_length), sizeof(blobs.at(i).m_length));
        }

        for (int i = 0; i < blobCount; i++) {
            blobs.at(i).read(s, replay.m_inputs);
        }

        return replay;
    }

    void write(std::ostream& s) { 
        // Unimplemented
    }
};

}

#endif SLC_HPP