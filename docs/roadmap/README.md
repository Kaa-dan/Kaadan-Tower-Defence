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
| [3](phase-03-content.md) | Data-driven content + hot reload | 3 d | ✅ done (`phase-03/content`, PR #5) |
| [4](phase-04-systems.md) | Systems split, damage pipeline, event queue | 2 d | ✅ done (branch pending) |
| [5](phase-05-renderer.md) | Renderer: textures, atlas, sprites | 5-6 d | 🔨 code complete; **owner visual check pending** |
| [6](phase-06-presentation.md) | Text, input, filesystem, interpolation | 4 d | ⬜ ⚠️ |
| [7](phase-07-persistence.md) | Save / replay / settings | 2 d | ⬜ |
| [8](phase-08-ui-audio.md) | UI framework, audio, scenes | 5-7 d | ⬜ |
| [9](phase-09-shipping.md) | Steam + packaging | 4-5 d | ⬜ |

⚠️ = the design for this phase has had only one pass. Phases 1-4 were reviewed by a second
independent design agent; the renderer review died mid-run. **It was redone on 2026-08-26** —
Phase 5's spec now carries eight marked corrections and two new ADRs. Phase 6 is still
one-pass and should get the same treatment before it starts.

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
- ~~**Re-run the renderer/platform design review** before Phase 5.~~ **Done 2026-08-26.** The
  headline finding: five of Phase 5's six acceptance criteria named tests that could not have
  compiled, because `ktd_tests` links only `ktd_game` and the `core` preset skips `engine/`
  entirely. Fixed by splitting the renderer at the line a test can reach
  ([ADR 0013](../decisions/0013-render-core-is-testable-without-a-gpu.md)). Also caught:
  `glDebugMessageCallback` is GL 4.3, and the first pass put it in the fail-hard loader list,
  which would have refused to start the renderer on every 3.3 driver.
- ~~**Decide on `stb_image.h`**~~ **Accepted 2026-10-16** ([ADR 0014](../decisions/0014-image-decoding.md)).
  Vendored v2.30 in `third_party/`, PNG only, reached solely through `engine/image/Image.cpp`.
  `loadTexture` and `buildAtlas` are built and tested; `CLAUDE.md`'s dependency line is edited
  to say so.
- **No art exists yet, so nothing calls `loadTexture`.** The path from a PNG on disk to a
  sprite on screen is complete and tested up to the GL upload, but `GameView` still draws
  coloured quads because there is nothing to load. The first art drop is a `GameView` change,
  not an engine one: `readFile` the PNGs, `decodePng`, `buildAtlas`, one `createTexture`, then
  `drawSprite` with the returned UVs. Art is the owner's.
- **Nobody has looked at the new renderer.** The GL half rewrote the vertex format, the draw
  path, the batching and the board upload. It runs clean, but screen capture is blocked by
  macOS permissions on this machine, so *"the game runs and looks correct"* is unverified and
  is Phase 5's last unticked criterion — and, since the decoder landed, **the only thing
  holding Phase 5 open.** Debug builds put draw calls and quad count in the window title.
- **Re-baselined the determinism hash.** Phase 2 moved the golden checksum to
  `0x8D8C026A6A51F571` and regenerated `tests/data/reference_trace.tsv`, on purpose and in one
  step: the goal-tunneling fix is a real gameplay change (enemies now leak reliably, so an
  undefended run loses lives slightly faster), and projectiles gained a serialized id. Any
  replay recorded before this is invalid. Everything after that step — the whole pool and
  index refactor — left the hash untouched.
- **Re-baselined the determinism hash again.** Phase 3 moved the golden checksum to
  `0x51D74AAE506DE6CA` and regenerated `tests/data/reference_trace.tsv`. Two causes, both
  intended: towers now serialize a type id where range and damage used to be, and the content
  fingerprint is the first field in the stream ([ADR 0010](../decisions/0010-content-fingerprint-in-the-hash.md)).
  Any replay recorded before this is invalid. The scripted scenario is unchanged and still
  builds only tower type 0, so the underlying run is the same run.
- **A balance edit now fails `determinism_golden` on purpose.** Changing a number in
  `content/` changes the content fingerprint and therefore every checksum. Regenerating the
  trace (`./build/core/tests/ktd_hashdump > tests/data/reference_trace.tsv`) and updating the
  constant in `tests/test_hash.cpp` is part of making a balance change, in the same commit.
- **The second tower type is untuned.** `content/towers.ktd` ships a "Cannon" so the data path
  demonstrably carries behaviour and not just numbers. Its cost, range, damage and reload are a
  starting point, not a balance decision — nobody has played against it.
- **Hot reload is untested by hand.** F5 builds, and its load ordering, error recovery and
  replay determinism are covered by tests, but nobody has pressed the key and watched a run
  rebalance. That is a playtest, which is the owner's.
- **Re-baselined the determinism hash a third time.** Phase 4 moved the golden checksum to
  `0xC420722146B89A12` and regenerated `tests/data/reference_trace.tsv`. Four causes, all
  intended: enemies and projectiles gained `previousPosition` (Phase 6's interpolation needs
  it), towers gained a serialized mod set, projectiles gained a damage type and a firing
  tower, and `content/combat.ktd` moved the content fingerprint. The scripted scenario is
  unchanged and still builds only tower type 0 against a grunt with no armor and no
  resistances, so the underlying run is the same run. Any replay recorded before this is
  invalid.
- **Burn and the resistance table are untuned, like the Cannon.** `content/combat.ktd` gives
  the cannon a fire damage type and a stacking burn, and `content/enemies.ktd` gives the runner
  50% fire resistance. They exist so the shipped content exercises the damage pipeline rather
  than only a test fixture. Nobody has played against any of it. The arrow tower — the only one
  the determinism scenario builds — deals physical to an unarmored grunt, so the shipped run's
  damage numbers are unchanged.
- **Victory is unreachable in the shipped level, on purpose.** `GameState::Victory` exists and
  is tested, but a level is only winnable when its content authors waves and declares no
  endless rule. `content/waves.ktd` declares one, so the shipped game is still pure endless.
  Whether the first campaign level ends is a design call.
- **Game speed is 1x/2x/3x on F1-F3 and has not been played with.** Fast-forward multiplies
  steps rather than lengthening dt, so a 3x run is bit-identical to a 1x one
  (`game_speed_multiplies_steps_not_dt` pins it). Whether three is the right top speed, and
  whether it should be on those keys, is feel.
- **`ItemDef` and `SkillNodeDef` were deliberately not written.** The Phase 3 spec lists them,
  but nothing consumes them and ADR 0003's behaviour verbs are Phase 4's work. An empty struct
  written now would fossilise a guess. Phase 4 wrote the damage types, resistances and status
  effects, because the damage pipeline consumes them; the behaviour verbs (splash, pierce,
  chain, on-kill) and the item and skill definitions that would carry them are still unwritten
  for the same reason as before. Same reasoning for damage types, resistances, status
  effects and targeting policies.
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
| ~~11~~ | ~~`docs/ARCHITECTURE.md`'s "Entity model" section still describes the pre-Phase-2 `std::vector` design~~ — rewritten in Phase 4 alongside the new "The tick" and "Damage" sections, because leaving a stale section next to a new one is worse than the drift | ~~`docs/ARCHITECTURE.md:58`~~ | ✅ 4 |
| 10 | The `ktd_tests` CTest list is scraped at **configure** time. Adding a `TEST()` to an existing file needs no CMake edit — but `cmake --build` alone will not register it, and the suite reports green while the new tests have never run. Reconfigure after adding tests | `tests/CMakeLists.txt:27` | unassigned |
| 5 | No render interpolation — visible judder above 60 Hz, which is most Steam users | `app/main.cpp:94` | 6 |
| ~~6~~ | ~~Goal detection is a radius test; fast enemies can tunnel past~~ — fixed in Phase 2: a cell test, which the movement clamp makes tunnel-proof | ~~`game/src/World.cpp:113`~~ | ✅ 2 |
| ~~7~~ | ~~`Rng::rangeInt` has modulo bias; seeds are not decorrelated~~ — fixed in Phase 1: Lemire's method + splitmix64 mixing | ~~`game/include/game/Types.h:52`~~ | ✅ 1 |
| 8 | `waveInProgress()` stays true forever once the state is `Defeat`, because `step()` returns early and the field never clears. Harmless in the game; it hangs any loop that waits on it. Now also true of `Victory` and `Paused` | `game/include/game/World.h`, `game/src/World.cpp` | unassigned |
| 12 | `sys::damage` emits an `EnemyDamaged` event for every hit, including hits that resolve to zero after armor. Presentation will want to distinguish "blocked" from "hit for nothing", and today it cannot without re-deriving the formula | `game/src/Systems.cpp` | unassigned |
| 9 | Targeting ties are **unreachable on the current level** — partially addressed in Phase 3: a wave can now mix enemy types at different speeds (`waves_run_authored_first_then_endless` covers the authoring), which is the mechanism that makes two enemies able to share a BFS distance at all. But the *shipped* level is still one lane at one speed, so a real run still never hits a tie. Closing this the rest of the way is level design, not code | `game/include/game/Targeting.h` | level design |

If you find a new one, add it here rather than fixing it inside an unrelated phase.
