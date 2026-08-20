# Phase 3 — Data-driven content

**Estimate:** 3 days · **Depends on:** Phase 2 · **Blocks:** Phase 4

## Goal

Get balance out of `Config.h` so the game can actually be built. This is the phase where
progress starts feeling fast — multiple tower and enemy types become an afternoon of data
editing rather than a code change.

Format decision: [ADR 0005](../decisions/0005-handrolled-data-format.md).
No scripting: [ADR 0003](../decisions/0003-no-embedded-lua.md).

## Acceptance criteria

- [ ] `parse_rejects_malformed_input_with_line_numbers` — errors name file, line, and column,
      and are **collected** rather than thrown at the first one.
- [ ] `parse_float_is_deterministic` — known decimal strings produce exact expected bit
      patterns. This is the criterion that justifies hand-rolling the parser.
- [ ] `content_unresolved_reference_is_an_error` — `projectile = "typo"` fails at load, loudly.
- [ ] `world_runs_from_a_database_built_in_code` — proves `game/` needs no filesystem.
- [ ] `two_tower_types_behave_differently` — the thing that is impossible today.
- [ ] `purity_stdlib` still passes — no file I/O leaked into `game/`.

## Work

**Layering** — the key constraint is that `game/` must not do file I/O, and the resolution is
that *parsing a string is not file I/O*:

- `game/content/Parser.h` — `std::string_view` → typed structs. Pure, and unit-testable from
  string literals without touching disk.
- `game/content/Database.h` — `ContentDb` with `TowerDef`, `EnemyDef`, `ProjectileDef`,
  `WaveDef`, `LevelDef`, `ItemDef`, `SkillNodeDef`.
- The platform layer reads bytes; `app/` wires them in. `World` holds a `const ContentDb*`.

**Two-pass load** — pass 1 registers `id` strings and assigns dense `uint16` indices; pass 2
resolves cross-references. After load, **strings never appear in the simulation**, which keeps
the tick allocation-free and the state hash trivially serializable.

**Entities store `TowerDefId`, not baked floats**, and read stats through the database. That is
what makes hot reload affect entities that already exist.

**Remove the `config::` defaults from struct member initializers** (`World.h:26-27`) — those
are precisely what makes multiple tower types impossible today.

**Level geometry** moves out of the `World` constructor (`World.cpp:14-20`) into a level file.

**Hot reload (F5)** — lives entirely in `app/`. Re-read, parse into a *fresh* database; on
parse failure keep the old one and surface errors without crashing (you will have a syntax
error half the time you press the key). On success, **re-simulate the current run's recorded
command log from tick 0 at max speed against the new data.**

That last part is the payoff and it is why scripting is not needed: hot reload does not mean
"new numbers from here on", it means *"show me this exact run, rebalanced."* Thirty minutes of
gameplay resimulates in well under a second. For a solo dev tuning 300 skill nodes this is the
highest-leverage tool in the whole roadmap.

## Content surface

Tower archetypes + upgrade tiers + sell; enemy archetypes with armor, resistances, flying,
boss, on-death behavior; projectile behaviors (splash, pierce, chain, beam); damage types;
status effects; targeting policies; wave tables with composition and boss waves; level
geometry; economy rules.
