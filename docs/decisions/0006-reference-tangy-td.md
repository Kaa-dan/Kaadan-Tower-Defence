# 0006 — Tangy TD as the design and feasibility reference

**Status:** accepted · 2026-08-20

## Context

kaadanTD needs a concrete model for both what to build and whether a solo from-scratch C++
game can succeed commercially.

## Decision

**Tangy TD by Cakez77 is the reference point.**

## The facts

- Steam app 2245620, released **2026-03-09** after roughly four years of full-time solo work
  (previously titled *Cakez TD*).
- Reported ~$30k revenue on day one; ~28,000 copies and ~$200k net in week one.
- "Very Positive" reviews.
- Built **from scratch in C++ with SDL and a hand-written OpenGL renderer** — no commercial
  engine. That is the same stack kaadanTD independently arrived at.
- The developer funded development from savings with family support.

## What to take from it

The praised features are a **300+ node Path-of-Exile-style skill tree** and **100+ combinable
items with synergies**. That is *depth through interacting systems*, not depth through content
volume — which is the cheapest kind of depth for a solo developer to produce, and the reason
the architecture prioritizes a stat-modifier layer and data-driven content over a level editor
or a large art pipeline.

## Calibration

Four years, full-time, with financial strain. Any estimate in this roadmap that implies
otherwise is measuring engineering work, not the whole game.

## Links

- https://store.steampowered.com/app/2245620/Tangy_TD/
- https://www.youtube.com/@Cakez77
- 4-year retrospective: https://www.youtube.com/watch?v=HF4g9pwK270
