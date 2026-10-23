# Scotykach

An Ableton Live plugin (VST3, AU, Standalone, for macOS) that runs the
firmware of the [Synthux Spotykach](https://github.com/Synthux-Academy/spotykach),
a two-deck looping and granular instrument.

## Status

Milestones S0 and S1 of [the plan](docs/PLAN.md) are done: the whole
firmware (v1.3.1), its audio engine and its own UI code, runs on a desktop
behind a plugin-ready processor. There is no plugin yet; that is S2 (JUCE).

- Reel, Slice and Drift record and loop, played with the module's own
  gestures: Alt+Play records, Alt+Seq arms the sequencer and holding Seq
  clears it, Grit and Flux work while held and lock with Alt, Tap sets the
  tempo.
- Every pot control is its own parameter (70 in all, `scotykach_render
  --list`), including the ones the module reaches by holding a button.
- The clock follows a host transport: Slice mode records and loops on the
  host's beats, and locks to its bars.
- Any sample rate (44.1 to 96 kHz tested), any block size, several
  instances in one process, MIDI notes and CCs at their exact sample.
- The firmware is built from a patched copy; the submodule itself is never
  edited. The [patches](docs/PLAN.md#firmware-patches) make x86 behave as
  the module's processor does where the two differ, let instances share a
  process, and give the plugin its one hook into the UI. With its UI, the
  firmware gives exactly the samples its engine alone gives.

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
./build/scotykach_render --list                   # every parameter, its range and default
./build/scotykach_render --in loop.wav --mute-from 2.5 \
    --at 0 a_mode=slice --at 0 a_mix=1 --at 0 crossfade=0 \
    --at 0.5 rec:A --at 2.5 rec:A --at 4 a_pitch=0.8 out.wav
./build/scotykach_render --rate 44100 --bpm 120 \
    --at 0 a_mode=slice --at 0.6 midi:B0,0E,7F --at 2.7 midi:B0,0E,7F out.wav
```

This runs the whole module through the plugin's processor. `--at T ACTION`
does an action at T seconds: `ID=VALUE` sets a parameter (switches by name,
as `a_mode=slice`), `press:ID`, `release:ID` and `tap:ID` work the buttons,
`rec:A` is Alt held while tapping Play, and `midi:B0,0E,7F` sends a MIDI
message (CC 14 on channel 1 records deck A). `--bpm` adds a host transport
for the clock to follow. Input can be any sample rate.

## Layout

- `external/spotykach/`, `external/DaisySP/` — the firmware (v1.3.1) and
  the DaisySP version it pins, as git submodules.
- `patches/` — changes applied to a copy of the firmware at build time.
- `cmake/Firmware.cmake` — applies them and builds the firmware libraries.
- `core/` — the desktop side: `shim/` (libDaisy stand-ins) and `overlay/hw/`
  (the hardware, replaced whole); `ScotykachModule`, the firmware as its
  `app.cpp` runs it; `ScotykachPanel`, the controls into it;
  `ScotykachParams`, the parameter table; `ScotykachProcessor`, what the
  plugin will wrap; `ScotykachEngine`, the engine alone, for tests.
- `tools/` — the renderer, the host rig it shares with the tests.
- `tests/` — engine, module, processor, parameter and resampler tests.
- `docs/PLAN.md` — the build plan and milestones.

## Licence

MIT (see [LICENSE](LICENSE)). The Spotykach firmware is MIT, copyright
Synthux Academy; DaisySP is MIT, copyright Electrosmith. The plugin will
use JUCE under its AGPLv3 licence, so a built plugin given to anyone else
would fall under the AGPL.
