# 0001 — C++ with a custom engine, not LÖVE

**Status:** accepted · 2026-08-20

## Context

With ~2,150 lines written, the question came up of abandoning the custom C++/SDL3/OpenGL
engine for LÖVE (love2d), a Lua game framework. LÖVE is free, zlib-licensed, and officially
supports Windows, macOS, Linux, Android, and iOS.

## Decision

**Stay with C++20 + SDL3 + the hand-written OpenGL renderer.**

## Why

- **The precedent is exact.** Tangy TD — the game this project is modelled on — was built
  from scratch in C++ with SDL and a hand-written OpenGL renderer by a solo developer, and
  shipped to ~$200k in its first week. See [0006](0006-reference-tangy-td.md).
- **The plumbing is already paid for.** SDL3 fetch-and-build, a batched GL renderer with its
  own loader, clean Platform/Renderer interfaces, and headless tests. What remains — towers,
  waves, items, balance — is gameplay logic that is no faster to write in Lua.
- **The tests would be lost.** `tests/` links only `game/` and runs without a window. That is
  the only ground truth available on an AI-built project.
- **SDL3 already reaches every target platform**, including iOS and Android. The mobile GL
  port is a shader-version change, roughly a day.
- **Consoles.** LÖVE has no console story at all. Consoles were later dropped from the
  priority list, which weakens but does not reverse this.

## Consequences

- Text rendering, audio, and asset loading must all be built. LÖVE would have provided them.
- Mobile store packaging is manual work whenever it happens.
- The "one dependency" property is preserved, and it keeps paying out — it is the reason
  there is no glad, no JSON library, and no Lua.

## If you are reconsidering this

The only genuine case for switching is *mobile-first, ship soon, consoles were never real*.
If that becomes true, it is a real conversation. Iteration speed is **not** a reason — that is
solved by hot-reloadable data files (Phase 3), not by an engine change.
