# 0014 — Image decoding: vendored `stb_image.h`

**Status:** proposed — owner's call · 2026-08-26

## Context

Phase 5 puts pixel art on screen, which means something has to turn a file into RGBA bytes.
`CLAUDE.md` opens by naming exactly one dependency, SDL3, and that is a claim about the project
worth defending rather than spending by accident.

Three options.

**Vendor `stb_image.h`.** One public-domain header, committed to the tree, no fetch step and no
version to track. Decodes PNG, which is what a pixel-art pipeline exports. It is ~7k lines of
someone else's C in the build, and it parses untrusted-shaped input — though on a shipped game
the images are ours and ship inside the package.

**Use SDL's `SDL_LoadBMP`.** Zero new dependencies; it is already linked. But BMP means no
alpha channel worth relying on, files roughly an order of magnitude larger in the shipped
package, and an art pipeline that exports PNG and then converts. It also puts image decoding
behind the `Platform` interface, which means the atlas packer's tests need a platform, which
Phase 5 just finished arranging not to need.

**Write a PNG decoder.** Inflate plus five unfilter cases is maybe two days for the subset that
matters, and it is two days spent on a solved problem instead of on the game. The temptation is
real and should be resisted.

## Decision

**Vendor `stb_image.h` in `third_party/`, compiled in exactly one translation unit under
`engine/`.** Recommended, not yet ratified — the dependency count is a stated project value and
changing it is the owner's call, not mine.

Constraints if accepted:

- `game/` may not reach it. It is not on `ktd_game`'s include path, and the existing
  `purity_platform` guard already fails on anything of this shape.
- Neither may `ktd_render_core` ([ADR 0013](0013-render-core-is-testable-without-a-gpu.md)) —
  the packer takes pixel buffers, never file paths, so decoding sits above it.
- Compiled with the project's warnings but **not** `-Werror`. It is not our code and a compiler
  upgrade should not break the build on a warning in a vendored header.
- Pinned by committing the file, with the version recorded in a comment. No FetchContent: a
  single header fetched at configure time is all of the cost and none of the benefit.

## Consequences

`CLAUDE.md`'s "One dependency: SDL3" becomes "One dependency: SDL3, plus one vendored header",
and that line has to be edited rather than quietly falsified.

Deferring is cheap and is the reason this does not block. Everything in Phase 5 except
`loadTexture`'s file path — the packer, the sort key, the batch machine, the vertex format, the
shader, indexed drawing — works against in-memory pixel buffers. The decoder can land last, and
the phase can begin before this is answered.
