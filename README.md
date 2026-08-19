# kaadanTD

A 2D tower defense game with a custom engine. No game engine, C++20, one
dependency (SDL3).

Target order: **macOS now → Windows next → Linux → mobile → consoles.**

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/app/kaadanTD
```

SDL3 is fetched and built automatically by CMake, so no package manager is
required on any platform.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Controls

| Input | Action |
|---|---|
| Left click | Build a tower on the hovered tile |
| Space | Pause |
| R | Restart |
| Esc | Quit |

Gold, lives, wave and kill count are shown in the window title until a text
renderer exists.

## Layout

```
game/      Pure C++ simulation. Zero platform or graphics dependencies.
           Compiles unchanged on every target, including consoles.
engine/    Platform + Renderer interfaces and their backends.
  platform/Platform.h    window, input, clock, present
  renderer/Renderer.h    camera, quads, lines
  backends/sdl/          the only file that knows an OS exists
  backends/gl/           OpenGL 3.3 core, batched; portable loader, no glad
app/       Wires the two together. Fixed-timestep loop and the game's drawing.
tests/     Headless tests. Link only game/, no window required.
```

## The one rule

**`game/` must never include a platform or graphics header.**

That is the entire portability strategy. The simulation compiles anywhere;
porting means writing one new `Platform` and one new `Renderer` implementation
and changing no game code. `ctest` enforces this — the `core_purity` test fails
the build if an SDL, Win32, Cocoa, X11, GL, Vulkan or D3D include appears under
`game/`.

The simulation is also deterministic: fixed 1/60s timestep and a seeded RNG, no
`rand()`, no wall-clock reads. Same seed and same inputs produce the same run on
every platform, which is what makes replays, save/resume and cross-platform
consistency possible later. A test asserts it.

## Porting later

- **Windows**: SDL3 and the GL loader already cover it; expect a build-system
  pass and input polish, not a rewrite.
- **Linux**: same.
- **Mobile**: SDL3 covers iOS/Android. The real work is touch controls and
  app-lifecycle handling, not the engine.
- **Consoles**: gated on being an approved developer with devkits, not on code.
  The layering above is what keeps that door open.

## Next steps

- Bitmap font renderer, so the HUD leaves the window title
- Textures and sprite atlas (`stb_image`) to replace coloured quads
- Tower selling/upgrading, multiple tower and enemy types
- Level loading from a data file instead of the hardcoded default board
- Audio (SDL3 provides the output; mixing is yours)
