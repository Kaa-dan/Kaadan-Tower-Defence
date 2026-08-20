# Phase 9 — Steam and shipping

**Estimate:** 4-5 days · **Depends on:** everything

Much of this phase is **owner work**, not code — accounts, certificates, store assets, and
identity that I have no access to.

## Acceptance criteria

- [ ] `ci_produces_installable_artifacts_for_three_platforms`.
- [ ] `save_path_resolves_correctly_in_a_bundled_build` — the classic "works in dev, breaks
      when packaged" failure.
- [ ] Manual (owner): Steam overlay opens; achievements fire; cloud saves sync; the build runs
      on a clean machine with no dev tools installed.

## Work

**Steamworks SDK** — achievements, cloud saves (replays and saves are small, ideal for this),
Rich Presence, Steam Input. **The overlay has specific GL requirements — test it early**, not
in the final week, because it can force renderer changes.

**macOS** — `.app` bundle, `Info.plist`, icon, universal binary
(`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`), `CMAKE_OSX_DEPLOYMENT_TARGET`, code signing, and
**notarization** — Gatekeeper blocks unnotarized apps outright, so this is not optional.

**Windows** — manifest, version resource, icon.

**Linux** — AppImage or a Steam depot layout.

**CI** — build all three; `install()` rules plus CPack.

## Owner-only

Steam Direct fee and app creation, store page copy and capsule art, pricing, an Apple Developer
account for notarization certificates, age ratings, and marketing. None of this is code, and
none of it is something I can do.
