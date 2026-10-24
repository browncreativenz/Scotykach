# Scotykach

An Ableton Live plugin (VST3, AU, Standalone, for macOS) that runs the
firmware of the [Synthux Spotykach](https://github.com/Synthux-Academy/spotykach),
a two-deck looping and granular instrument.

## Status

Milestones S0 to S2 of [the plan](docs/PLAN.md) are done: the whole
firmware (v1.3.1), its audio engine and its own UI code, runs as a VST3, AU
and Standalone plugin. It passes `pluginval` at its strictest level and
`auval`. The window has both decks' buttons, lit as the module's LEDs
are, above a list of the knobs (a panel-style window is S4).

- Reel, Slice and Drift record and loop, with a Record button that steps
  through recording, Clear, and a button or switch for each thing the
  module does with Alt. Grit and Flux work while held, Tap sets the tempo.
- Every pot control is its own parameter, including the ones the module
  reaches by holding a button, and so is everything the module does with
  Alt (91 parameters in all, `scotykach_render --list`).
- The clock follows Live: Slice mode records and loops on Live's beats,
  and locks to its bars.
- A saved set reopens with both decks' loops, at full quality.
- Any sample rate (44.1 to 96 kHz tested), any buffer size, several
  instances, MIDI notes and CCs at their exact sample.
- The firmware is built from a patched copy; the submodule itself is never
  edited. The [patches](docs/PLAN.md#firmware-patches) make x86 behave as
  the module's processor does where the two differ, fix three firmware bugs
  that automation can reach (a frozen LED loop, two of them; a MIDI CC
  writing past the decks), let instances share a process, and give the
  plugin its one hook into the UI.

## Build

Requires CMake 3.22 or later, a C++17 compiler (Xcode's command-line tools
on macOS) and `patch`.

```sh
git submodule update --init external/spotykach external/DaisySP   # first checkout only
cmake -B build -DCMAKE_BUILD_TYPE=Release                         # fetches JUCE 8.0.16 the first time
cmake --build build --parallel
ctest --test-dir build
```

The build installs the plugin for your user: on macOS the VST3 into
`~/Library/Audio/Plug-Ins/VST3` and the AU into
`~/Library/Audio/Plug-Ins/Components`. The Standalone app is in
`build/plugin/Scotykach_artefacts/Release/Standalone`.

- To use a JUCE checkout you already have, add
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.
- To build only the firmware, renderer and tests, without JUCE, add
  `-DSCOTYKACH_BUILD_PLUGIN=OFF`.
- `-DSCOTYKACH_SANITIZE=ON` runs the tests under AddressSanitizer and
  UndefinedBehaviorSanitizer, as CI does.
- On Linux, JUCE needs the ALSA, X11 (with Xext, Xinerama, Xrandr,
  Xcursor), FreeType, fontconfig and OpenGL development packages.

Or skip building: every push runs the **Build** workflow on GitHub Actions,
and its run page has a `Scotykach-macOS` download with the VST3, AU and
Standalone app. Those are signed ad hoc, so after unzipping, clear the
quarantine flag before Live will load them:

```sh
xattr -dr com.apple.quarantine Scotykach.vst3   # likewise Scotykach.component, Scotykach.app
```

## Using it in Live

After installing, rescan plug-ins (Settings > Plug-Ins > Rescan). Scotykach
is under Plug-Ins, by browncreativenz, as a VST3 and an AU. Put it on an
audio track: that track's audio is what the decks record.

- **The window** has both decks side by side. Buttons act while the
  mouse button is down, like the module's pads (hold **Grit** or **Flux**
  to play the effect). Each deck shows its LED ring and a line saying what
  it is doing. There is no Alt: everything the module does with Alt held
  has its own button or switch. Below them are the knobs and settings.
- **Recording:** **Record** steps through recording, and its label says
  what the next press does:
  - **Record** arms the deck; recording starts at the next sound (Reel,
    Drift) or the next beat (Slice). While waiting, the button reads
    **Cancel**.
  - **Close loop** ends the recording; the loop plays.
  - **Overdub** records on top of the playing loop, and **Stop overdub**
    stops. While overdubbing, each pass is a little quieter (Feedback,
    0.95 by default, as on the module).
  - **Clear** empties the deck. A deck holds up to 42 s; a recording that
    runs that long closes the loop itself.
  - **Record the deck** (a switch) records the deck's own output instead
    of the input, as Alt + Rev does on the module.
  - **Overdub after recording** (off by default) makes a closed loop go
    straight on overdubbing, as the module does.
  **Play** starts and stops the loop, **Rev** plays it backwards.
- **The other Alt functions:** **Seq Rec** arms the sequencer (then Seq
  records steps), **Seq Clear** clears it; **Grit lock** and **Flux lock**
  keep the effect on; **Quantise pitch** snaps Pitch to steps; **Sync mod**
  syncs the modulator to the clock; **Free size** stops Size snapping to
  slices.
- **Mapping in Live:** every button and switch is a parameter (Record,
  Clear, Play and so on, per deck). Add them to Live's device panel with
  Configure, then map them with Cmd-M (MIDI) or Cmd-K (keys). Record,
  Clear, Seq Rec and Seq Clear act on every press.
- **Every knob, layers included,** is its own parameter: A Feedback (SOS
  with Alt), A Grit Mix (SOS with Grit held), Tempo (Mod Freq with Tap
  held) and so on. Each automates and maps on its own.
- **Clock:** *Live* (the default) follows Live's tempo and transport;
  Slice mode's loops and quantised recording lock to Live's bars from the
  first bar line after you press Play in Live. While Live is stopped the
  module runs at Live's tempo on its own clock. *Internal* uses the Tempo
  parameter and Tap.
- **MIDI:** on a MIDI track, set MIDI To to the track holding Scotykach.
  Notes on channel 1 trigger deck A, channel 2 deck B, at their pitch
  (middle C plays at 1x). The module's CC map works too: CC 14 records,
  CC 85/86 play forward/reverse, CC 20–28 the deck controls, CC 102–109 the
  FX, CC 3 the crossfader.
- **Saving:** a set saves both loops at full quality (up to about 32 MB per
  instance) and reopens with them, stopped, as the module is after loading
  a loop; press Play.
- Latency is reported to Live, which compensates for it: 2 ms at 48 kHz,
  about 5 ms at 44.1 or 96 kHz.

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
`rec:A` is Alt held while tapping Play (the module's record gesture;
`tap:a_record` is the Record button), and `midi:B0,0E,7F` sends a MIDI
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
  plugin wraps; `ScotykachEngine`, the engine alone, for tests.
- `tools/` — the renderer, the host rig it shares with the tests.
- `plugin/` — the JUCE plugin.
- `tests/` — engine, module, processor, parameter, resampler, stress and
  plugin tests.
- `docs/PLAN.md` — the build plan and milestones.

## Licence

MIT (see [LICENSE](LICENSE)). The Spotykach firmware is MIT, copyright
Synthux Academy; DaisySP is MIT, copyright Electrosmith. The plugin uses
JUCE under its AGPLv3 licence, so a built plugin given to anyone else would
fall under the AGPL.
