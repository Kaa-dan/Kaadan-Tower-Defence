# 0003 — Content in data files, no embedded scripting

**Status:** accepted · 2026-08-20

## Context

A 300+ node skill tree and 100+ combinable items need fast iteration. Balance changes should
not require a recompile. Embedding Lua is the standard answer — the engine stays C++, and
scripts define behavior.

Note this is a different question from [0001](0001-cpp-not-love2d.md): that was about writing
the *game* in Lua. This is about embedding Lua *inside* the C++ engine.

## Decision

**Data files parsed into plain C++ structs. No scripting runtime.**

## Why

- **It breaks the "one dependency" property**, which has been driving good decisions —
  the hand-rolled GL loader exists because of it.
- **It is the one piece that is genuinely cheap to add later.** Determinism and entity storage
  are expensive to retrofit; scripting is contained if the data layer is clean. So there is no
  reason to pay for it now.
- **Type checking is lost exactly where it is most needed.** A typo in an item-synergy script
  becomes a runtime bug at wave 40 instead of a compile error.
- **Determinism.** A scripting runtime is another surface where evaluation order and number
  handling must be audited for bit-exactness.

## Consequences

- ~20-30 code-level behavior "verbs" (splash, pierce, chain, on-kill) plus numeric stat
  modifiers must cover the whole item and skill design space. This is sufficient for 100+
  combinable items, but it is a real constraint on design.
- Hot reload of data files (Phase 3) is what delivers the iteration speed instead.
- **Do not** make the data format expressive enough to define new verbs. That is Lua with
  worse tooling and no debugger.
