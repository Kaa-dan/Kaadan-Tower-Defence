# 0014 — Image decoding: vendored `stb_image.h`

**Status:** accepted · proposed 2026-08-26, ratified by the owner 2026-10-16 (one amendment, below)

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
`engine/`.** Ratified by the owner on 2026-10-16. The dependency count is a stated project
value, which is why this waited for them rather than being decided in a phase.

Constraints if accepted:

- `game/` may not reach it. It is not on `ktd_game`'s include path, and the existing
  `purity_platform` guard already fails on anything of this shape.
- Neither may `ktd_render_core` ([ADR 0013](0013-render-core-is-testable-without-a-gpu.md)) —
  the packer takes pixel buffers, never file paths, so decoding sits above it.
- ~~Compiled with the project's warnings but **not** `-Werror`.~~ **Amended at
  implementation:** `third_party/` is a `SYSTEM` include directory, so the header's own
  warnings are suppressed entirely (plus `#pragma warning(push, 0)` for MSVC), and the wrapping
  TU, `engine/image/Image.cpp`, keeps the full set *with* `-Werror`. Same goal — a compiler
  upgrade cannot break the build on a warning in someone else's code — but the literal version
  would have printed hundreds of `-Wconversion` lines from stb into every build, which trains
  everyone to stop reading warnings, and would have dropped `-Werror` from our own code in that
  TU for no reason.
- Pinned by committing the file, with the version recorded in a comment. No FetchContent: a
  single header fetched at configure time is all of the cost and none of the benefit.
  *As built:* the file is byte-identical to upstream (v2.30, `nothings/stb@013ac3b`); the
  version and SHA-256 live in `third_party/README.md` and in the comment above the include, so
  an upgrade is a clean diff against upstream.

## As built

- **`ktd_image`** (`engine/image/Image.{h,cpp}`) is a separate target that links only
  `ktd_game`, built outside the `KTD_TESTS_ONLY` guard — the same move as ADR 0013, so the
  decoder's tests run in the core preset. It takes bytes, never a path, and is configured
  `STBI_ONLY_PNG`, `STBI_NO_STDIO`, and `STBI_MAX_DIMENSIONS 8192`; a `static_assert` keeps that
  cap equal to the `kMaxImageDimension` the header advertises.
- **`loadTexture`** (`engine/image/LoadTexture.cpp`) is the join — `readFile` → `decodePng` →
  `createTexture` — and is compiled into `ktd_engine`, the one place that has a `Platform` and a
  `Renderer` at once.
- **`buildAtlas`** (`engine/renderer/AtlasBuilder.h`) composes decoded sprites into one texture.
  It takes pixel buffers, so it lives in `ktd_render_core` without reaching the decoder.
- **Enforced, not just stated:** `purity_platform` and `purity_render_core` now ban
  `stb_image` includes, and a new `purity_image` holds `engine/image/` to the no-backend rule.

## Consequences

`CLAUDE.md`'s "One dependency: SDL3" becomes "One dependency: SDL3, plus one vendored header",
and that line has to be edited rather than quietly falsified.

Deferring is cheap and is the reason this does not block. Everything in Phase 5 except
`loadTexture`'s file path — the packer, the sort key, the batch machine, the vertex format, the
shader, indexed drawing — works against in-memory pixel buffers. The decoder can land last, and
the phase can begin before this is answered.
