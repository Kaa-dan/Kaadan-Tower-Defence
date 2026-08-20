#pragma once

// Canonical serialization of the world, and a hash over it.
//
// The hash is what makes determinism testable: comparing two runs field by
// field with a tolerance can only ever prove they are similar, and the whole
// point of this phase is that "similar" is not good enough.
//
// It hashes a CANONICAL SERIALIZATION, never the raw bytes of the structs.
// Hashing struct memory would be faster and wrong: padding bytes are
// uninitialised and their contents are not portable, so two identical runs on
// two compilers would report different hashes and the test would fail for a
// reason that has nothing to do with the simulation. It would also break every
// time a field was reordered.
//
// The serializer is deliberately not private to the hash. Phase 7's save format
// writes the same byte stream to a different sink, which means the save
// serializer is exercised by every test run in this phase rather than sitting
// unwritten until someone tries to load a save and discovers it was wrong.

#include "game/World.h"

#include <bit>
#include <cstdint>

namespace ktd {

// FNV-1a, 64-bit. Chosen for being short enough to read and verify by eye
// rather than for collision resistance -- this detects accidental divergence,
// not adversarial tampering.
class Hasher {
public:
    void u8(std::uint8_t value) {
        state_ ^= value;
        state_ *= 0x100000001B3ull;
    }

    void u32(std::uint32_t value) {
        // Little-endian byte order, written out rather than memcpy'd, so the
        // result does not depend on the host's endianness.
        u8(static_cast<std::uint8_t>(value & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    }

    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
        u32(static_cast<std::uint32_t>(value >> 32));
    }

    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

    // Floats are hashed by their bits, because two floats that compare equal
    // are not necessarily the same run: +0.0 and -0.0 are equal and have
    // different bits, and NaN is unequal to itself. Both are canonicalised so
    // the hash tracks the simulation's value rather than an artefact of how it
    // was produced.
    void f32(float value) {
        std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        const std::uint32_t exponent = (bits >> 23) & 0xFFu;
        const std::uint32_t mantissa = bits & 0x7FFFFFu;
        if (exponent == 0xFFu && mantissa != 0) {
            bits = 0x7FC00000u;  // one canonical quiet NaN
        } else if (bits == 0x80000000u) {
            bits = 0u;  // -0.0 hashes as +0.0
        }
        u32(bits);
    }

    std::uint64_t value() const { return state_; }

private:
    std::uint64_t state_ = 0xCBF29CE484222325ull;
};

// The canonical byte stream itself lives on World::serialize(), next to the
// fields it writes -- a serializer in another file is a serializer that gets a
// field added behind its back.
inline std::uint64_t hashWorld(const World& world) {
    Hasher hasher;
    world.serialize(hasher);
    return hasher.value();
}

}  // namespace ktd
