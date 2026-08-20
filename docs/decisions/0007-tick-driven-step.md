# 0007 — step() takes no dt; the tick is the clock

**Status:** accepted · 2026-08-20

## Context

`World::step(float dt = config::kFixedDt)` accepted the timestep as an argument, with a comment
asking callers not to pass a variable frame delta. Timers inside the simulation — tower
cooldown, spawn interval, wave gap, projectile age — were floats decremented by that `dt`.

Both are reproducibility hazards, and neither was enforced by anything.

## Decision

**`void World::step()`. The simulation owns its clock as an integer tick counter, and every
duration is a `uint32` tick count.**

`config::kFixedDt` survives for the spatial math inside a tick (`speed * dt`). It is a
constant, not a knob. The app-side accumulator remains a `double` and the two never meet
implicitly.

## Why

A parameter nothing enforces is a convention, not a property. The only thing preventing a
variable delta from reaching the simulation was that everyone remembered a comment — and
conventions do not survive a debugging session where someone wants to see what happens at half
speed.

Float timers are the subtler half. A timer decremented by `dt` accumulates rounding error
proportional to how many times it has been decremented, so "6 seconds between waves" means
something measurably different on the 200th wave than on the first. Worse, how much it drifts
depends on instruction selection, which makes it a cross-platform divergence and not merely an
inaccuracy. Integer tick counts are exact by construction and cost nothing.

Durations stay authored in seconds where they are designed — `config::ticksFromSeconds` does
the conversion once, at compile time — so the design intent stays readable.

## Consequences

- A design value that is not a whole number of ticks cannot survive the conversion.
  `kTowerShotsPerSecond = 1.6` is 37.5 ticks; it became `kTowerReloadTicks = 38`, an actual
  rate of 1.579/second. Any future tunable expressed as a rate has the same constraint, and it
  is better to author the tick count directly than to discover the rounding later.
- Anything that wants wall-clock seconds converts at the boundary, for display only
  (`World::timeUntilNextWave`).
- Slow-motion, fast-forward and frame-stepping are all "call `step()` a different number of
  times", which is the only implementation that keeps a run reproducible.
- The tick counter is part of the state hash and of the save format, so a save resumes at the
  right point in the schedule rather than approximately.
