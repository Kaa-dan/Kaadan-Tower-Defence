# 0011 — One damage formula, resolved in a canonical order

**Status:** accepted · 2026-08-26

## Context

Phase 4 gave the game a damage pipeline: stat modifiers on towers, status effects on enemies,
resistances and armor. Every one of those is a multiplication or an addition applied to a
float, and every one of them arrives from a source that can fire in a different order from one
run to the next.

Two orderings are load-bearing and neither is naturally canonical:

- **Which multipliers apply first.** An enemy's status effects live in an inline slot array, and
  a slot is filled by whichever effect landed first. So walking the slots means multiplying in
  the order the debuffs happened to arrive.
- **Which hits land first.** Projectiles are visited in pool order, and pool order is a function
  of destruction history ([ADR 0009](0009-handles-address-ids-identify.md)). Two shots hitting
  one enemy in a tick would subtract in an order set by which projectile died first.

Float arithmetic is not associative. `x * a * b` and `x * b * a` differ in the last bit for most
values of `a` and `b`, and so do `(h - p) - q` and `(h - q) - p`. A last-bit difference in
remaining health decides whether an enemy dies this tick or next, and the difference compounds
from there.

## Decision

**Every point of damage goes through one function, in one written-down order, and both
orderings above are made canonical rather than incidental.**

The formula, in full:

```
base
  + flat mods                                   ]  resolveStats(), cached per tower
  x (1 + summed percent mods)                   ]
  x status multipliers, ascending StatusDefId   ]  resolveDamage(), per hit
  x (1 - resistance to this damage type)        ]
  - armor                                       ]
  clamped at 0                                  ]
```

- **Status multipliers iterate by ascending `StatusDefId`, never by slot.** At most one slot
  holds a given def, so def id is a total order with no tie to break.
- **Mods are summed in ascending `(StatId, ModOp, value bits)`, never in insertion order.**
- **The damage queue is sorted before it drains**, on
  `(targetId, sourceId, kind, type, amount bits)` — stable identities only, never handles or
  array positions.
- `stable_sort` throughout, because `std::sort` is banned in `game/`.

## Why

- **It is a player-visible correctness property, not only a determinism one.** Chill-then-hex
  and hex-then-chill must take the same number off the health bar. When they do not, the
  difference is far too small to see in one hit and large enough to change a breakpoint over a
  wave, which is exactly the shape of bug that reaches a forum as "damage feels inconsistent"
  and cannot be reproduced by the person reporting it.
- **It is the same argument as ADR 0009, applied to arithmetic.** That ADR removed storage
  layout from gameplay tie-breaks. This removes it from damage.
- **Armor after every multiplier is a design decision, written down.** It makes armor worth most
  against many small hits and least against one big one. That is a real balance lever, and it is
  the sort of thing that otherwise becomes true by accident because of how an expression was
  typed and then cannot be changed without breaking every tuned number.
- **A queue rather than direct subtraction is what makes the formula auditable.** There is one
  place health goes down. A new damage source cannot skip resistance because its author did not
  know resistance existed.

## Consequences

- A projectile carries its tower's *resolved* damage, damage type and applied status, set when
  the shot is fired. The tower may be sold before the shot lands, so reading back from it is not
  an option — and the flat/percent stages are cached per tower anyway.
- Damage-over-time from a status is attributed to no source (`sourceId` stays invalid). An effect
  outlives the tower that applied it, and putting a dead entity's id in the sort key would make
  the order depend on when that tower was sold.
- Two sorts run per tick — the damage queue, and each tower's mod set on the ticks its mods
  change. Both are over tiny arrays. If either ever shows up in a profile, the fix is a
  pre-sorted insert, not dropping the ordering.
- Adding a status def in the middle of the content file changes every `StatusDefId`, and
  therefore the multiplication order, and therefore the hash. That is already true of every
  content edit ([ADR 0010](0010-content-fingerprint-in-the-hash.md)).
