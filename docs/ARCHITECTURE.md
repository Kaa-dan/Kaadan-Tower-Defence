# Architecture

## The shape

Three layers, one direction of dependency:

```
game/     Pure simulation. Rules, not pixels. Links nothing.
   ↑
engine/   Platform + Renderer interfaces and their backends. May see game/.
   ↑
app/      Wires them together. Fixed-timestep loop, camera, drawing.
```

`tests/` links only `game/`, so the entire simulation is verifiable without a window.

## The one rule

**`game/` must never include a platform or graphics header.**

That single restriction is the whole portability strategy. Porting to a new target means
writing one `Platform` implementation and one `Renderer` implementation and changing no game
code. It is enforced by `purity_platform` in CTest, not by discipline, because discipline
fails at 2am.

The corollary matters as much: **`engine/` may see `game/`'s public headers, never the
reverse.** The moment the simulation knows what a window is, the property is gone.

## Determinism

Same seed plus the same input log produces a **bit-identical** run on every platform.

This is not an aesthetic preference. It buys four concrete things:

- **Replays as a shipping feature** — leaderboard verification, sharing, Steam integration.
- **Bug reports that reproduce.** A player sends a few KB and it plays back exactly.
- **Tiny saves** — an input log rather than a full state dump.
- **Regression tests with real teeth** — every fixed bug becomes a recorded replay that
  fails if it ever returns.

It is also the single most expensive property to retrofit, which is why it is being hardened
in Phase 1 rather than "later". Once replays ship, changing RNG consumption order or float
behavior becomes a compatibility break rather than a refactor.

The approach is **float with discipline, not fixed-point** — see
[ADR 0002](decisions/0002-float-not-fixed-point.md). The constraints that follow from it live
in `.claude/rules/game-core.md` and are enforced by `purity_determinism`.

## Fixed timestep

The simulation advances in exact fixed steps regardless of render rate. `app/main.cpp` owns an
accumulator, clamped so that a stall — a window drag, a breakpoint, a laptop sleep — cannot
trigger a hundred-step catch-up spiral.

A variable frame delta must never reach `World::step()`. Phase 1 removes the `dt` parameter
entirely so that it cannot.

## Entity model

`Pool<T, Tag>`: dense storage behind a sparse slot table, with **generation-tagged handles** —
dense iteration, O(1) lookup, and a stale handle that resolves to `nullptr` instead of
dangling. Not a general ECS; see [ADR 0004](decisions/0004-pools-not-ecs.md). A uniform grid
(`SpatialIndex`) is rebuilt once per tick so targeting is a bucket query rather than a scan.

Every entity has **two names, and the distinction is load-bearing**
([ADR 0009](decisions/0009-handles-address-ids-identify.md)). A `Handle` is a storage address:
which slot an entity occupies is a function of the pool's destruction history, so a handle
must never be serialized and must never decide a gameplay tie-break. An `EntityId` is a
monotonic per-pool sequence number that is never recycled, and it is what the hash writes, what
the save file will carry, and what every ordering rule sorts on.

Entities store a **def id rather than baked stats**, so a hot reload reaches towers already
standing on the board. Per-entity state that is genuinely state — health, cooldown, position,
status slots, stat mods — lives on the entity.

## The tick

`World` owns a `WorldState` (a plain struct with public fields and no invariants) and runs a
fixed list of free functions over it. `World::step()` *is* the scheduler: nine calls in a
readable order, fixed at compile time. ADR 0004 chose that over a runtime scheduler
deliberately — a list of calls is deterministic by being obvious, and there is nothing to
audit.

The order is itself a design decision at every line: commands, waves, statuses, enemy movement,
index rebuild, towers, projectiles, damage, outcome. An enemy that moved after the towers
looked for it would be shot at where it used to be; damage applied before the projectiles moved
would land a tick early.

**Intent goes in through a command queue; facts come out through an event queue**
([ADR 0012](decisions/0012-commands-in-events-out.md)). The UI previews, enqueues, and learns
the outcome from an event — it never calls into the simulation to change it. That is what makes
a replay file exactly the command stream plus tick numbers. Events are write-only from the
simulation's side, cleared at the top of every step, and hashed anyway, because a run that
reaches the same state for a different reason has a bug in it.

## Damage

Every point of damage in the game goes through **one function in one written-down order**
([ADR 0011](decisions/0011-damage-order-is-canonical.md)):

```
base -> + flat mods -> x (1 + summed percent) -> x status multipliers -> x (1 - resistance)
     -> - armor -> clamp at 0
```

Two orderings inside that are made canonical rather than left incidental, because float
arithmetic is not associative and both would otherwise leak storage layout into balance. Status
multipliers apply in ascending `StatusDefId`, never in the order the debuffs landed. The tick's
damage queue is sorted on stable identities before it drains, never left in pool order.

Status effects are a fixed-size inline array on the enemy, and stat modifiers a fixed-size
inline array on the tower. No heap on a gameplay path, and no allocator-dependent iteration
order. How an effect stacks — refresh, stack intensity, stack duration, ignore — is a line in
the content file, not a branch in the code.

## Content

Balance lives in `content/*.ktd` and is read through `content::ContentDb`. `Config.h` retains
only the tick rate — everything a designer would want to change is data.

`game/` owns the schemas and the parser; the platform layer reads the bytes, because **parsing
a string is not file I/O** and the purity rule must hold. The format is a documented TOML
subset with a hand-written deterministic float parser
([ADR 0005](decisions/0005-handrolled-data-format.md)); there is no scripting runtime
([ADR 0003](decisions/0003-no-embedded-lua.md)).

Two properties make the rest work. Loading is **two-pass** — ids registered and assigned dense
`uint16` indices first, cross-references resolved second — so file order never matters and no
string survives into the simulation. And **entities store a def id rather than baked stats**,
so a hot reload reaches towers already standing on the board rather than only the ones built
after it.

Because balance is data, it is also part of what makes a run a run: the state hash covers a
fingerprint of the whole database ([ADR 0010](decisions/0010-content-fingerprint-in-the-hash.md)).

Hot reload (F5) lives entirely in `app/`. It re-reads the files into a *fresh* database, keeps
the old one if parsing fails, and on success replays the run's recorded command log from tick 0
against the new numbers. The point is "show me this exact run, rebalanced" rather than "new
numbers from here on", and the command log it replays is the same structure Phase 7 writes to
disk as a replay file.

## Rendering

One draw call per frame today: every quad and line appends to a single CPU vertex buffer that
is uploaded and drawn in `endFrame`. There are **no textures at any level** — not in the API,
the vertex format, the shader, or the GL loader's entry-point list. Phase 5 changes all four
together, because none of them works alone.

OpenGL 3.3 core, with a hand-rolled ~30-entry-point loader instead of glad or GLEW. That is a
deliberate cost paid to keep the dependency count at one.

## Dependencies

**SDL3, pinned to a commit SHA, plus one vendored header: `stb_image.h`.** That is the entire
list, and it is a design constraint rather than an accident — it is why there is no glad, no
JSON library, and no Lua. The header is the accepted compromise because the alternative was
writing a PNG decoder ([ADR 0014](decisions/0014-image-decoding.md)). It lives unmodified in
`third_party/`, is compiled in exactly one translation unit (`engine/image/Image.cpp`), and is
reachable from neither `game/` nor `ktd_render_core` — the purity guards fail the build if
either tries.

## Where the reasoning lives

Decisions with lasting consequences are recorded in [`decisions/`](decisions/). If you are
about to reverse one, read it first — the reasoning is there, and if it is genuinely wrong,
say so explicitly rather than diverging quietly.
