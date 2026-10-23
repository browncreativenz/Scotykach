# Scotykach

An Ableton Live plugin (VST3, AU, Standalone, for macOS) that runs the
firmware of the [Synthux Spotykach](https://github.com/Synthux-Academy/spotykach),
a two-deck looping and granular instrument.

## Status

Milestone S0 of [the plan](docs/PLAN.md) is done: the firmware's audio
engine (v1.3.1) runs on a desktop.

- Reel, Slice and Drift record and loop. Slice locks recording and
  playback to the clock, as on the module.
- Output is the same whatever block size the host uses, and several
  engines in one process are independent, as plugin instances in Live are.
- The firmware is built from a patched copy; the submodule itself is never
  edited. The [patches](docs/PLAN.md#firmware-patches) make x86 behave as
  the module's processor does where the two differ, and let engines share
  a process. Tests show the second kind changes nothing about the sound.

There is no plugin yet: that is S1 (the module's controls and Live's clock)
and S2 (the JUCE plugin).

## Build

Requires CMake 3.22 or later, a C++17 compiler (Xcode's command-line tools
on macOS) and `patch`.

```sh
git submodule update --init external/spotykach external/DaisySP   # first checkout only
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

Add `-DSCOTYKACH_SANITIZE=ON` to run the tests under AddressSanitizer and
UndefinedBehaviorSanitizer, as CI does.

## Render audio from the command line

```sh
./build/scotykach_render out.wav                  # demo: deck A loops 2 s of a test signal
./build/scotykach_render --in loop.wav --mute-from 2.5 \
    --at 0 mode:A=slice --at 0 sos:A=1 --at 0 crossfade=0 \
    --at 0.5 rec:A --at 2.5 rec:A --at 4 pitch:A=0.8 out.wav
```

`--at T ACTION` applies an action at T seconds. Pads are `rec:A`,
`recint:A` (record the other deck), `play:A` and `rev:A`; knobs take a
position from 0 to 1 (`pos`, `size`, `pitch`, `env`, `sos`, `fb`, `win`,
`envsize`, `gritamt`, `fluxamt`, `crossfade`); switches are `mode:A=reel|slice|drift`
and `route=stereo|mono|gen`; also `grit:A=on`, `flux:A=on` and `tempo=120`.
The full list is in [tools/Script.h](tools/Script.h). Input must be 48 kHz
for now.

## Layout

- `external/spotykach/`, `external/DaisySP/` — the firmware (v1.3.1) and
  the DaisySP version it pins, as git submodules.
- `patches/` — changes applied to a copy of the firmware at build time.
- `cmake/Firmware.cmake` — applies them and builds the firmware libraries.
- `core/` — `ScotykachEngine`, which runs the firmware as the module's
  interrupts do, and `shim/`, the desktop stand-ins for libDaisy.
- `tools/` — the renderer and its scripting.
- `tests/` — scenario tests, the multi-instance test and the comparison
  with the firmware.
- `docs/PLAN.md` — the build plan and milestones.

## Licence

MIT (see [LICENSE](LICENSE)). The Spotykach firmware is MIT, copyright
Synthux Academy; DaisySP is MIT, copyright Electrosmith. The plugin will
use JUCE under its AGPLv3 licence, so a built plugin given to anyone else
would fall under the AGPL.
