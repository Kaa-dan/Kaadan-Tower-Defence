#pragma once

// The hash function used for the determinism fingerprint, on its own so that
// anything with a canonical serialization can be hashed without dragging in
// World.h. Hash.h pairs it with the world; content/Database.h uses it for the
// content fingerprint, and World.h cannot include Hash.h without a cycle.

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

}  // namespace ktd
