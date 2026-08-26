# 0015 — Sprites take a direction, not an angle

**Status:** accepted · 2026-08-26

## Context

The first pass of the Phase 5 spec gave `drawSprite` a `float rotation` parameter, which is what
almost every 2D sprite API does. The second pass caught that it is the wrong parameter *for this
codebase*, for a reason specific to what the simulation already keeps.

There is exactly one rotated thing on screen today: the tower barrel, drawn at `GameView.cpp:129`
from `tower.aim`. `aim` is a unit vector, maintained by the targeting system as a vector, because
that is what the targeting maths produces. Nothing in `game/` has ever held an angle.

With an angle API the call path is: the simulation produces a unit vector, the drawing code calls
`atan2` to turn it into an angle, the renderer calls `sin` and `cos` to turn it back into a unit
vector, and then rotates. Two transcendental functions to recover a value that was already in
hand, once per rotated sprite per frame.

The second cost is worse than the arithmetic. An angle in the renderer's API is a standing
invitation to put trig into `game/Math.h` so callers can produce one — and `purity_determinism`
bans `<cmath>` from `game/` precisely because `std::sin` is not required to give the same result
on two platforms, which is the whole basis of ADR 0002's float-with-discipline position. Phase 1
declined to add `sinTurns`/`cosTurns` for exactly this reason, with no caller asking for them. An
angle-shaped renderer API creates the caller.

## Decision

**`drawSprite` takes `Vec2 forward`, a unit direction. `{1, 0}` is unrotated.**

Rotating a corner by a unit direction is a 2x2 matrix multiply:

```
(x·f.x − y·f.y, x·f.y + y·f.x)
```

Four multiplies and two adds, no trig anywhere, and `{1, 0}` is bit-exactly the identity — which
matters, because an axis-aligned tile that lands a hair off its neighbour seams at any zoom.
`quadCorners` in `renderer/Vertex.h` is the whole implementation and
`sprite_forward_rotates_without_trig` is the test.

A caller that genuinely has an angle builds the vector itself, in presentation code, where trig
is allowed and where nobody's replay depends on the answer.

## Consequences

- No trig in `game/`, and no pressure to add any. The purity guard keeps holding for a reason
  rather than by luck.
- `drawLine` becomes a rotated quad rather than a hand-computed normal, so the rotation path is
  exercised by every line the game already draws instead of only by tower barrels.
- A degenerate `forward` — the zero vector `normalized` returns for a zero-length input —
  collapses the quad to a point and draws nothing. For a line from a point to itself that is the
  right answer. For a sprite it would be a silent disappearance, so callers pass a real direction
  or `{1, 0}`.
- An animation system that wants to interpolate rotation will want angles or complex-number slerp
  at that point. It can have them, in presentation, and hand a direction to this API.
