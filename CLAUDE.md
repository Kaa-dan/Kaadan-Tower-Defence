# kaadanTD

A 2D tower defense with a custom C++20 engine. **Commercial Steam release** — not a learning
project. One dependency: SDL3.

## The one rule

**`game/` must never include a platform or graphics header.** That is the entire portability
strategy, and `ctest` enforces it. See `docs/ARCHITECTURE.md` for the reasoning.

## Layout

| Path | Contents |
|---|---|
| `game/` | Pure simulation. Zero platform or graphics dependencies. Deterministic. |
| `engine/` | `Platform` + `Renderer` interfaces and their SDL3/OpenGL backends. |
| `app/` | Wires the two together. Fixed-timestep loop and the game's drawing. |
| `tests/` | Headless. Links only `game/`, no window required. |
| `content/` | Balance and level data (`*.ktd`). Edit these, not `Config.h`. F5 reloads them. |

## Build and test

```sh
cmake --preset core && cmake --build --preset core && ctest --preset core   # sim only, ~5s
cmake --preset dev  && cmake --build --preset dev                           # full app + SDL
./build/dev/app/kaadanTD
```

Use the `core` preset by default — it skips SDL entirely (0.7s to configure vs 94s).
`asan` preset for sanitizers. All 78 tests must stay green.

CTest entries are scraped at **configure** time, so re-run `cmake --preset core` after adding
a `TEST()` — otherwise the suite reports green without ever running the new one.

Formatting is CI-enforced. The version is pinned deliberately — clang-format's output shifts
between major releases, so it must move in lockstep with `.github/workflows/ci.yml`:

```sh
find game engine app tests \( -name '*.h' -o -name '*.cpp' \) -print0 \
  | xargs -0 -r pipx run clang-format==20.1.7 -i     # -i rewrites in place;
                                                    # --dry-run --Werror is the CI check
```

## Working agreement

- **One roadmap phase per session.** `/clear` at phase boundaries.
- `/status` — where we are. `/phase` — start the next one. `/ship-phase` — branch, commit, PR.
- Never commit or push unless asked. Never merge to `main`.
- Delegate broad reading to Explore subagents; keep their file dumps out of this context.
- Write a `docs/JOURNAL.md` entry before context runs low, not after.
- Record non-obvious decisions as ADRs in `docs/decisions/` while the reasoning is still live.

## Where things are

- **What to work on next:** `docs/roadmap/README.md` (status ledger + known bugs)
- **Why it's built this way:** `docs/ARCHITECTURE.md` and `docs/decisions/`
- **What happened last session:** `docs/JOURNAL.md`
- **Human-facing overview:** `README.md`

Detailed constraints for `game/` and `engine/` load automatically from `.claude/rules/` when
you touch those directories.

## What I cannot do

Playtesting and game feel, design and balance calls, art and audio, and the Steam business
side belong to the owner. Tests are the only ground truth available here — so when adding
behavior, add the test that proves it.
