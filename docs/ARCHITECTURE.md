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

Currently plain `std::vector<Enemy|Tower|Projectile>` inside `World`, with a monotonic
`EntityId`. This is correct for a prototype and wrong at commercial scale: it cannot survive
area damage (raw pointers into a mutating vector) and lookup is a linear scan.

Phase 2 moves to pooled storage with **generation-tagged handles** — dense iteration, O(1)
lookup, and a stale handle that fails safely instead of dangling. Not a general ECS; see
[ADR 0004](decisions/0004-pools-not-ecs.md).

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

**SDL3, pinned to a commit SHA.** That is the entire list, and it is a design constraint
rather than an accident — it is why there is no glad, no JSON library, and no Lua. Single-
header libraries (stb) are the accepted compromise when the alternative is writing an image
decoder.

## Where the reasoning lives

Decisions with lasting consequences are recorded in [`decisions/`](decisions/). If you are
about to reverse one, read it first — the reasoning is there, and if it is genuinely wrong,
say so explicitly rather than diverging quietly.
