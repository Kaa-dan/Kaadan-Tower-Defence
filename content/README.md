# Content

Balance and level data. Editing a file here changes the game without a recompile, and **F5 in
the running game reloads it** — re-simulating the current run from tick 0 against the new
numbers, so you see this exact run rebalanced rather than "new numbers from here on".

If a file has a syntax error, F5 prints the problems and keeps the run going on the old data.

## Files

| File | Holds |
|---|---|
| `level.ktd` | Board size, the wall layout, spawn and goal, starting gold and lives |
| `towers.ktd` | Projectiles, and the towers that fire them |
| `enemies.ktd` | Enemy archetypes |
| `waves.ktd` | Spawn timing, authored waves, and the endless progression |

Load order does not matter. References are resolved in a second pass, so a tower may name a
projectile defined in another file or further down.

## Format

A small, deliberately boring subset of TOML. Anything outside it is an error rather than a
guess — including a **misspelled key**, because a `dammage = 40` that silently does nothing is
the worst kind of balance bug.

```toml
# comments run to end of line

[tower.arrow]           # a table; the name after the dot is the id things refer to
name = "Arrow Tower"    # strings are raw bytes between quotes -- no escape sequences
cost = 40               # integer
range = 3.2             # float
reload_ticks = 38       # durations are in ticks; 60 ticks = 1 second
projectile = "bolt"     # a reference, resolved at load
enabled = true          # boolean

[[wave]]                # double brackets mean "there can be several of these"
enemies = ["grunt", "runner"]
counts  = [4, 2]        # arrays are flat and all one type; they may span lines
```

Errors name the file, line and column, and **all** of them are reported at once rather than
one per attempt.

### Numbers

Durations are authored in **ticks**, not seconds, because the simulation counts ticks and a
duration that is not a whole number of them cannot survive the conversion. 60 ticks is one
second. See [ADR 0007](../docs/decisions/0007-tick-driven-step.md).

Decimals are converted by a hand-written parser so the result is bit-identical on Windows,
macOS and Linux ([ADR 0005](../docs/decisions/0005-handrolled-data-format.md)). It **refuses**
numbers it cannot convert exactly: at most 15 significant digits, and an exponent within ±22.
If a value is rejected, write fewer digits.

### Level maps

`rows` is one string per row, top row first, one character per tile:

- `.` — open: enemies walk here, towers can be built here
- `#` — wall: permanent, never walkable

The loader checks that the map is the right size and that a route from spawn to goal exists,
so a missing `.` fails the build instead of producing enemies that stand still.

## A word on determinism

Changing any number here changes what every recorded replay means, so the state hash covers
the content ([ADR 0010](../docs/decisions/0010-content-fingerprint-in-the-hash.md)). After a
balance edit, `determinism_golden` will fail on purpose. Regenerate the trace and update the
constant in the same commit:

```sh
./build/core/tests/ktd_hashdump > tests/data/reference_trace.tsv
# then paste the last line's hash into kGoldenChecksum in tests/test_hash.cpp
```

Display names are the one exception — they are not part of the run, so translating the game
does not invalidate replays.
