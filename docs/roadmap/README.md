# Roadmap

Ten phases from prototype to a shippable commercial architecture. Each phase leaves the build
green and the game playable, and ships as one PR the owner reviews and merges.

Start a phase with `/phase <n>`. Check state with `/status`.

## Status ledger

| Phase | Scope | Est. | State |
|---|---|---|---|
| [0](phase-00-infrastructure.md) | Infrastructure: presets, CI, `-Werror`, purity guards | ½ d | ✅ done (`340f0a7`) |
| [1](phase-01-determinism.md) | Determinism hardening | 2 d | ✅ done, **CI verified** |
| [2](phase-02-entity-storage.md) | Entity storage: handles, pools, spatial index | 3 d | ✅ done (`phase-02/entity-storage`, PR #4) |
| [3](phase-03-content.md) | Data-driven content + hot reload | 3 d | ⬜ **next** |
| [4](phase-04-systems.md) | Systems split, damage pipeline, event queue | 2 d | ⬜ |
| [5](phase-05-renderer.md) | Renderer: textures, atlas, sprites | 4-5 d | ⬜ ⚠️ |
| [6](phase-06-presentation.md) | Text, input, filesystem, interpolation | 4 d | ⬜ ⚠️ |
| [7](phase-07-persistence.md) | Save / replay / settings | 2 d | ⬜ |
| [8](phase-08-ui-audio.md) | UI framework, audio, scenes | 5-7 d | ⬜ |
| [9](phase-09-shipping.md) | Steam + packaging | 4-5 d | ⬜ |

⚠️ = the design for this phase has had only one pass. Phases 1-4 were reviewed by a second
independent design agent; the renderer review died mid-run and was never redone. Worth
re-running before starting Phase 5.

Roughly **6-8 weeks** of focused work to the end of Phase 6, at which point the engine can
carry a commercial game and the rest is content and polish. For calibration: Tangy TD took
about four years ([ADR 0006](../decisions/0006-reference-tangy-td.md)).

## Ordering constraints

Two dependencies are load-bearing:

- **Determinism (1) before entity storage (2)** — the state hash is what proves the storage
  refactor changed no behavior. Without it you are trusting that 24 tests cover everything.
- **Content (3) before the systems split (4)** — otherwise the damage pipeline gets written
  against hardcoded constants and immediately rewritten.

## Open items (owner decisions, not phase work)

- ~~**Push `phase-01/determinism` and watch the CI run.**~~ **Done, and it passed.** CI first
  executed on 2026-08-22 against PR #4. The `determinism-compare` job reports *"All 6 traces
  agree over 3000 ticks: macos-debug, macos-release, ubuntu-debug, ubuntu-release,
  windows-debug, windows-release"*, and the follow-up diff against the committed
  `reference_trace.tsv` is clean. ADR 0002's float-with-discipline decision is now a measured
  property rather than a hope. The Linux and Windows legs are green generally, and the `app`
  job's X11/Wayland package list built on a real runner.
- **Re-tune the two shifted timers.** The tick conversion moved the spawn interval from 46 to
  45 ticks (waves arrive ~2.2% faster) and projectile lifetime from 181 to 180. Both are
  correct engineering and neither is a design decision anyone made. Balance is the owner's
  call.
- **`LICENSE`** — deliberately not chosen. This is a commercial title; the call is the
  owner's.
- **Re-run the renderer/platform design review** before Phase 5.
- **Re-baselined the determinism hash.** Phase 2 moved the golden checksum to
  `0x8D8C026A6A51F571` and regenerated `tests/data/reference_trace.tsv`, on purpose and in one
  step: the goal-tunneling fix is a real gameplay change (enemies now leak reliably, so an
  undefended run loses lives slightly faster), and projectiles gained a serialized id. Any
  replay recorded before this is invalid. Everything after that step — the whole pool and
  index refactor — left the hash untouched.
- **`stress_5000_enemies` does not step the real `World`.** Nothing today can put 5,000
  enemies into a world: wave size is a function of the wave number and lives run out first. It
  drives the real `Pool`, `SpatialIndex` and targeting rule instead. A true end-to-end stress
  needs an authored starting state, which Phase 3 provides for free.

## Known-wrong today

Bugs in the current code, not missing features. Each names the phase that fixes it.

| # | Issue | Where | Fixed in |
|---|---|---|---|
| ~~1~~ | ~~`std::pow` diverges across platforms~~ — fixed in Phase 1: `math::powi`, computed once per wave | ~~`game/src/World.cpp:74-75`~~ | ✅ 1 |
| ~~2~~ | ~~`findTarget` returns a raw `const Enemy*` into a vector~~ — fixed in Phase 2: it returns an `EnemyId` handle, and the projectile loop borrows through an epoch-checked `Ref`. Tie-breaks resolve on the stable entity id ([ADR 0009](../decisions/0009-handles-address-ids-identify.md)) | ~~`game/src/World.cpp`~~ | ✅ 2 |
| ~~3~~ | ~~O(P×E) double linear scan per frame~~ — fixed in Phase 2: one O(1) handle lookup per projectile, and targeting queries a tile-bucket index | ~~`game/src/World.cpp:197,202`~~ | ✅ 2 |
| ~~4~~ | ~~`canPlaceTower` copies the whole `Grid` and flood-fills every frame~~ — fixed in Phase 2: `World::canPlaceAt` treats one cell as blocked instead of copying, reuses its BFS buffers, and memoizes on `(cell, gridVersion)`. `placement_preview_allocates_nothing` pins it | ~~`game/src/Path.cpp:94`~~ | ✅ 2 |
| 5 | No render interpolation — visible judder above 60 Hz, which is most Steam users | `app/main.cpp:94` | 6 |
| ~~6~~ | ~~Goal detection is a radius test; fast enemies can tunnel past~~ — fixed in Phase 2: a cell test, which the movement clamp makes tunnel-proof | ~~`game/src/World.cpp:113`~~ | ✅ 2 |
| ~~7~~ | ~~`Rng::rangeInt` has modulo bias; seeds are not decorrelated~~ — fixed in Phase 1: Lemire's method + splitmix64 mixing | ~~`game/include/game/Types.h:52`~~ | ✅ 1 |
| 8 | `waveInProgress()` stays true forever once `state_` is `Defeat`, because `step()` returns early and the field never clears. Harmless in the game; it hangs any loop that waits on it | `game/include/game/World.h`, `game/src/World.cpp:25-27` | unassigned |
| 9 | Targeting ties are **unreachable on the current level** — one lane, one spawn per 45 ticks, one speed per wave, so no two enemies ever share a BFS distance. The tie-break rule is correct and tested directly, but nothing in a real run exercises it. Not a bug today; it means a level with branching routes or mixed speeds ships an untested-in-anger code path | `game/include/game/Targeting.h` | 3 |

If you find a new one, add it here rather than fixing it inside an unrelated phase.
