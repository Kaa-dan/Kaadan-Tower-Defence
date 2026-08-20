# 0008 — Hash a canonical serialization, not struct bytes

**Status:** accepted · 2026-08-20

## Context

Phase 1 needed a state hash: a single value that says whether two runs are the same run. The
obvious implementation is to hash the bytes of the entity vectors directly — one `memcpy` per
vector, no per-field code to maintain, and fast.

## Decision

**`World::serialize()` writes an explicit, ordered byte stream, and the hash is taken over
that. The serializer lives on `World`, next to the fields it writes, and is templated on its
sink so the Phase 7 save writer is the same code.**

## Why

Hashing struct memory is faster and wrong.

- **Padding is not portable.** `Enemy` and `Tower` contain padding bytes whose contents are
  unspecified and, in practice, whatever was in that memory before. Two bit-identical runs on
  two compilers would report different hashes, and the determinism test would fail for a
  reason that has nothing to do with the simulation. Chasing that once costs more than writing
  the serializer.
- **Field order would become load-bearing invisibly.** Reordering members for packing would
  silently change every hash.
- **The interesting state is not all in the vectors.** RNG stream positions, the wave
  director's timers, and the grid live elsewhere. A byte hash of the entity arrays would have
  missed them, and the test would have passed while the simulation diverged.

Two floats that compare equal are not necessarily the same state, so floats are hashed by
bits, with `-0.0` folded to `+0.0` and NaN payloads collapsed to one canonical quiet NaN. Both
are cases where the bits differ but the simulation's value does not.

Integers are written little-endian byte by byte rather than `memcpy`'d, so the hash does not
depend on host endianness. Nothing targeted today is big-endian; the cost is a few lines and
it removes a class of surprise from any future port.

## Consequences

- **Adding a member to `World` means adding a line to `serialize()`.** A field missing from it
  is a divergence the hash cannot see, which is precisely the failure this whole phase exists
  to prevent. It lives in `World.h` beside the members so the omission is visible during
  review rather than discovered by a desynced replay.
- The save format and the determinism hash cannot drift apart, because they are one
  implementation with two sinks — and the save serializer is exercised by every test run in
  Phase 1 rather than sitting unwritten until Phase 7.
- **Field order is the format.** Appending is safe; reordering or removing changes every hash
  and invalidates every recorded replay. That is what `simVersion` will exist to detect.
- FNV-1a was chosen for being short enough to verify by reading. This detects accidental
  divergence, not tampering; a save-file integrity check against a cheating player is a
  different problem needing a different function.
