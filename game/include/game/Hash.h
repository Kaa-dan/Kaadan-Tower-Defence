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

#include "game/Hasher.h"
#include "game/World.h"

#include <cstdint>

namespace ktd {

// The canonical byte stream itself lives on World::serialize(), next to the
// fields it writes -- a serializer in another file is a serializer that gets a
// field added behind its back.
inline std::uint64_t hashWorld(const World& world) {
    Hasher hasher;
    world.serialize(hasher);
    return hasher.value();
}

}  // namespace ktd
