# Scotykach: build plan

Scotykach is a desktop audio plugin that runs the firmware of
[Spotykach](https://github.com/Synthux-Academy/spotykach), Synthux Academy's
two-deck looping and granular instrument, in Ableton Live.

It follows the approach of the Capicola plugin
([browncreativenz/Granular-plugin](https://github.com/browncreativenz/Granular-plugin)),
which is finished and in use in Live. Its JUCE setup, resampler, MIDI-out
port and macOS CI are copied into this repo, not shared, so the two
projects stay independent.

## What the firmware is

- Repository: `github.com/Synthux-Academy/spotykach`, tag `v1.3.1`
  (commit `bdc0f77`), the release Synthux lists as Spotykach v1.3.1. Newer
  `v1.4.0.b*` betas exist.
- Licence: **MIT** (Synthux Academy). Its dependencies are DaisySP (MIT,
  pinned at `599511b`) and a libDaisy fork by bleeptools (MIT). The firmware
  uses none of DaisySP's LGPL modules. Its copyright notices stay with
  its code.
- Hardware: Daisy Seed (STM32H750, 480 MHz), 48 kHz, 96-sample blocks.
  2 stereo decks, 12 touch pads, 15 pots, 7 CV inputs, 2 gate inputs and
  outputs, a clock input, 2 LFO/follower CV outputs, MIDI over USB and DIN,
  two 32-LED rings, and an SD card for saved loops.
- Code layout:
  - `src/core/` (about 5,000 lines): the audio engine. `Core` holds two `Deck`s
    (buffer, detector, grain `Generator` with voices, `Track` sequencer,
    `Fx` with Grit and Flux), two `Modulator`s (LFO or envelope follower),
    a `Panner`, a `Click` and a `Driver` (clock, tempo, key intervals). The
    deck modes are Reel, Slice and Drift. The routings are Stereo, Double
    Mono and Generative Stereo.
  - `src/ui/` (about 2,500 lines): `CoreUI` turns pots, pads, switches, CV
    and MIDI into engine calls, and draws the LEDs. Much of the instrument's
    behaviour lives here: pad gestures, Alt and Shift combinations, pitch
    quantising, the speed map and the factory values.
  - `src/hw/`, `src/memory/`: drivers, the SDRAM pool, SD-card and flash
    storage.

## Feasibility check (done)

The firmware's `src/core` was built on Linux with g++ and none of its files
were changed:

- 25 of its 27 `.cpp` files compiled once a short stub header was added.
  The stub supplies the few libDaisy pieces they use (`System::GetNow`,
  `StopwatchTimer`, `ScopedIrqBlocker`, `Logger`) and the standard headers
  that the ARM toolchain pulls in indirectly.
- The other two, `core.cpp` and the memory pool, reach the SD-card driver
  through `hw/buffer.sdram.h`. A heap-backed pool with the same interface
  replaced that header, and then everything linked against DaisySP.
- A test program set Deck A to Reel and gave it the UI's factory values.
  It pressed Record twice: the first press armed the deck and the second
  closed the loop. The deck recorded a 1.99 s loop and played it back with
  the input silent.
- It rendered 6 s of audio in under 0.1 s (one deck playing). CPU will not
  be a problem.

So the engine itself needs no porting, only a thin shell around it. The real
work is outside `src/core`: the clock, the controls, and saving state.

S0 then found a handful of places where the firmware relies on how the
Cortex-M7 behaves, and one that stops two engines sharing a process. Those
are fixed with small patches (below), not by editing the submodule.

## Decisions

| Topic | Choice |
|---|---|
| Use | Personal use only; no public release |
| Name | Scotykach, after this repo, so it isn't confused with Synthux's Spotykach in Live's plug-in list |
| Host | Ableton Live on macOS; VST3 (main), AU, Standalone |
| Repo | This repo, standalone. `Resampler.h`, `RateBridge.h`, the MIDI-out port and the CI workflow are copied from Granular-plugin |
| Firmware | `v1.3.1` as a git submodule (`external/spotykach`), plus DaisySP at the firmware's pinned commit (`external/DaisySP`). The submodule is never edited: the build compiles a copy with `patches/` applied (see Firmware patches) |
| Engine rate | Always 48 kHz, 96-sample blocks, as on the hardware. `RateBridge` converts other host rates |
| Controls | Run the firmware's own `CoreUI` against a desktop `Hardware` class, so that every gesture and LED behaves as on the module. Each layered pot control is its own Live parameter (decided) |
| Clock | Follows Live's tempo and transport by default; the module's internal clock is an option |
| Saved loops | Recorded audio is saved in the Live set with the plugin state, at full quality: 32-bit float, the recorded part only (decided) |
| Outputs | Gate outs, LFO outs and clock out as MIDI on a virtual port, as Capicola's TRIG and ENV outputs are |
| Licence | Source stays MIT, like the firmware. JUCE's open-source licence is AGPLv3, so a built plugin given to anyone else would fall under the AGPL; for private use that asks nothing of you |

## Architecture

```
CMakeLists.txt              top level (S2 adds JUCE 8.0.16-security)
cmake/Firmware.cmake        patched copies of the firmware, DaisySP, engine libraries
external/spotykach/         submodule, v1.3.1 (never edited)
external/DaisySP/           submodule, at the firmware's pinned commit
patches/portability/        make x86 behave as the module's Cortex-M7 does
patches/instances/          let several engines share one process
core/                       desktop shell, plain C++17, no JUCE
  shim/                     libDaisy stand-ins: System and StopwatchTimer (sample
                            clock), ScopedIrqBlocker, Logger; prelude.h (force-included)
  shim/Context.h            per-engine clock and memory pool, current per thread
  shim/hw/buffer.sdram.h    the firmware's memory pool, on the heap, one per engine
  ScotykachEngine.h/.cpp    the module's interrupts in order, 96-sample FIFO   (S0)
  Resampler.h, RateBridge.h host rate <-> 48 kHz, copied from Granular-plugin  (S1)
  shim/hw/hardware.h        Hardware: pots, CV, pads, switches, gates, LEDs     (S1)
  shim/memory/              Storage and Settings backed by plugin state         (S1)
  ScotykachProcessor.h/.cpp host-facing: rate bridge, clock sync, parameters    (S1)
plugin/                     JUCE AudioProcessor and editor                      (S2)
tools/Script.h              text actions on the engine (rec:A, mode:A=slice, ...)
tools/render/               scotykach_render: scripted actions, WAV in and out
tests/                      scenario tests, multi-instance test, firmware comparison
.github/workflows/          CI: Linux GCC, Clang and sanitizers; macOS
```

Shim headers come first on the include path. The firmware's own includes,
such as `"hw/buffer.sdram.h"` or `<daisy_seed.h>`, then resolve to the
desktop versions.

## Firmware patches

At configure time the build copies the firmware's `src/` into the build
tree and applies the patches, in order. If a patch no longer applies (after
a firmware update, say), configuring stops and names it. Each changed line
carries a `Desktop (Scotykach)` comment.

| Patch | Why |
|---|---|
| `portability/0001-arm-modulo-by-zero` | In Slice mode with nothing recorded, `Generator::apply_dimensions` takes `% 0`. The Cortex-M7 doesn't trap on that (`x % 0` is `x`); x86 crashes. `arm_mod()` gives the module's result |
| `portability/0002-repeatable-panner` | The Generative Stereo panner draws from `std::random_device`, which reads OS entropy on a desktop, so no two renders were alike. A default-seeded `std::mt19937` makes renders and Live exports repeat |
| `portability/0003-arm-float-to-int` | Overdubbing at exactly 1x divides by zero in the write-head skip, and the empty-buffer playhead is 0/0. The module converts inf and NaN to integers by saturating (inf to 0xFFFFFFFF, NaN to 0); GCC and Clang on x86-64 each give other values, which took the other branch. `arm_f2u32()` and `arm_f2s32()` convert as the module does |
| `portability/0004-clang-array` | `Flux::switch_mode` initialises a variable-length array, a GCC extension Clang rejects. A `constexpr` count gives the same array |
| `instances/0001-per-instance-buffer-tables` | `Core::init` kept its buffer tables in function statics, so a second engine in the process wrote into the first one's loops (and `Detector` keeps the table's address). They become `Core` members |

DaisySP and the firmware also build with `-fwrapv`: DaisySP's `WhiteNoise`
relies on signed overflow wrapping, as it does on the module.

`scotykach_fw_single` is the firmware with the portability patches only:
the closest the desktop gets to the module. The `same_as_firmware_*` tests
render every scenario through it and through the full build (one per
process, since unpatched it supports only one engine) and require the same
bytes, so the instance patch provably changes nothing about the sound.

The sanitizer CI job (`-DSCOTYKACH_SANITIZE=ON`) runs every test under
AddressSanitizer and UndefinedBehaviorSanitizer, including out-of-range
float-to-integer conversions. That is how patches 0001 and 0003 were found,
and it fails on any new report, so S1's `src/ui` gets the same check.

### What else S0 found

- **Zeroed memory.** On the module the engine objects are statics, so
  whatever their constructors leave alone starts at zero (`Core` never sets
  `_route`, for one). The engine builds `Core` in zeroed memory to match.
- **The CV inputs matter even unpatched.** `CoreUI::read_cv` writes the
  V/Oct speed multiplier every block; without it, Slice mode played every
  slice at speed 0. The engine reads its CV inputs every block as
  `read_cv` does (all zero, a multiplier of 1, until S1 makes them
  parameters).
- **Where the clock ticks.** `Driver::tick` is called from `CoreUI::tick`
  in the audio callback, not from the 500 Hz timer, which only prepares the
  generators, reads the gate inputs and refreshes the LEDs. Both run every
  2 ms, so the rate is the same.

### Timing: one firmware "tick" per 96 samples

On the hardware, several things run at different rates:

| Hardware | Rate | In the plugin |
|---|---|---|
| TIM5 interrupt: `process_gate_in`, `Core::prepare`; LEDs every 4th call | 500 Hz = every 2 ms | once per block, since 96 samples at 48 kHz is exactly 2 ms |
| Audio callback: `ProcessAnalogControls`, `CoreUI::tick` (MIDI, then `Driver::tick`: the clock), `read_cv`, `Core::process` | every 96 samples | the same, on each full 96-sample block |
| Main loop: `CoreUI::process` (pots, pads, switches) | as fast as it can | once per block |
| DAC callback: `Modulator::process`, the two CV outputs | 48 kHz | 96 times per block |

Everything runs in that order on the audio thread, one block at a time.
That removes the races between interrupts and the main loop, and makes
renders repeatable. The shim's `System::GetNow()` and `StopwatchTimer`
(and in S1 `GetTick()`) count engine samples instead of wall-clock time. Hold
times, tap tempo and the clock timeout then behave the same in an offline
export as in real time.

A 96-sample FIFO decouples the engine from host block sizes, as Capicola's
64-sample FIFO does. That adds 2 ms of latency, reported to Live, plus
about 2.7 ms for resampling at rates other than 48 kHz.

### Clock sync with Live

`Driver` already accepts an external clock at 24 PPQN, its MIDI clock mode.
The processor reads Live's tempo and beat position each block and makes
24 PPQN ticks at the exact sample where each falls. It also sends Start on
the transport's start and Stop on its stop. Quantised record, Slice mode's
grid and the sequencer then lock to Live's bars. When Live's transport is
stopped, the module's own internal clock can be used instead.

## Parameters

The 15 pots, the switches and the pads, taken from `Hardware.h` and
`CoreUI::_init_values` (v1.3.1). Parameters are 0..1 positions, as on the
module; `CoreUI` applies its own curves.

Each pot does several jobs, depending on what is held while it turns
(`CoreUI::_process_ui_queue`):

| Pot (per deck) | Alone | With Alt | With Grit / Flux held | With Tap held |
|---|---|---|---|---|
| SOS | in/out mix | overdub feedback | FX mix | key interval |
| Pos | start | start offset (once there is a cue) | Grit character / Flux feedback | |
| Size | size (Slice: quarters) | window size (Drift) | | |
| Pitch | speed, or pitch in Slice | quantised | Grit / Flux intensity | |
| Env | envelope shape | envelope size (Drift) | | |
| Mod Freq | mod speed | | | tempo |
| Mod Amount | mod amount | | | click level |

Plus the Crossfade pot (shared), and these:

| Group | Parameters (per deck A/B, unless noted) |
|---|---|
| Switches | Route (Stereo, Double Mono, Generative Stereo, shared); deck mode (Reel, Slice, Drift); mod type (LFO square, LFO random, Follow) |
| CV inputs | Size/Pos, Mix, V/Oct; Crossfade (shared): automatable, like Capicola's Mod In |
| Pads and buttons | Play, Rev, Grit, Flux, Seq per deck; Spot; Alt; Tap: button parameters plus MIDI notes |

**Decided: each layered control is its own parameter** (SOS Feedback,
Grit Mix, Tempo and so on), so each automates and MIDI-maps on its own in
Live. On the module the layers use pick-up (`MValue`): a value moves only
once the pot passes it, so nothing jumps when you switch layers. With a
parameter per control there is nothing to pick up, so S1 adds a small
adapter that writes each parameter straight to its `MValue`. The window can
still show the module's layers.

MIDI into the plugin works as on the module: `CoreMIDI` handles its note map
and its CC map (`config.h`: CC 3 crossfade, 20–28 deck controls, 85/86
direction, 102–109 FX, and so on), on channels 1 and 2 by default.

## Milestones

| # | Milestone | Where | Done when |
|---|---|---|---|
| S0 ✅ | Submodules; patches; `shim/` with heap pool and sample clock; firmware `src/core` built into `scotykach_fw`; `ScotykachEngine`; `scotykach_render` with scripted actions; CTest scenarios; CI on Linux (GCC, Clang, sanitizers) and macOS | Cloud session | Reel, Slice and Drift record and loop; output is identical whatever the host block size; engines are independent in one process; the instance patch changes no output; no sanitizer reports |
| S1 | Desktop `Hardware`; firmware `CoreUI` and `CoreMIDI`, with instance patches for the statics they keep (tap and clock-edge state, LED animation); `Storage` and `Settings` backed by memory; `ScotykachProcessor` with rate bridge, FIFO and Live clock sync | Cloud session | Gesture tests pass (hold to clear, Alt combos, quantised record locked to a simulated host transport); multi-rate checks pass |
| S2 | JUCE plugin with generic controls; pads as buttons and MIDI notes; state save and load including recorded loops; macOS CI with `auval` and `pluginval` | Cloud session, then your Mac | Loads in Live, records and loops in time with Live's tempo, and a saved set reopens with its loops |
| S3 | Gate outs, LFO outs and clock out as MIDI on a "Scotykach 1" virtual port, using the MIDI-out code copied from Capicola | Cloud plus your Mac | Gate notes play a Drum Rack; LFO CCs MIDI-map in Live |
| S4 | The plugin window, from a mockup you approve first: 12 pads, two 32-LED rings drawn from the firmware's own LED output, pots, switches, crossfader | Cloud session builds it and renders screenshots; your Mac for feel | Looks like the mockup, and the LEDs match the module |
| S5 (optional) | WAV import and export of loops (the firmware has `memory/wav.cpp`), drag and drop with Live; SD-card slots as loop banks | Cloud plus your Mac | A Live clip dropped on a deck plays; a deck loop drags out as a clip |

## Risks and open questions

- **Set size.** Loops are saved at full quality (decided): each deck holds
  up to 42 s of stereo audio, about 16 MB as floats, so a Live set grows by
  up to about 32 MB per instance, by less for shorter loops. Live may be
  slower to save such sets; S2 checks how it copes.
- **Pads and a mouse.** Many gestures press two pads at once: Alt plus a
  pad, holding Play, Seq combinations. With a mouse that means a latching Alt
  and Shift in the window. MIDI notes from a pad controller allow true
  chords; a Push or any pad controller is the natural way to play it.
- **Firmware behaviour tied to timing.** `CoreUI` measures holds and
  blinks with libDaisy timers. Driving those from the sample clock keeps
  them right, but S1's gesture tests should confirm hold-to-clear and tap
  tempo.
- **Random sequences differ by platform.** Drift's grain positions use
  `std::default_random_engine` and `std::normal_distribution`, which
  libstdc++ (Linux, and the module's toolchain) and libc++ (macOS)
  implement differently. Each platform is repeatable and the grains sound
  the same, but the exact sequence on a Mac is not the module's.
- **Memory.** Two 42 s stereo buffers, about 32 MB, plus delay lines, per
  instance (the firmware's third, for undo, is unused in v1.3.1). They are
  allocated when the engine is created, never on the audio thread.
- **Calibration and boot.** The module calibrates its CV inputs and plays
  a boot animation. The shim reports calibration as done and moves
  `CoreUI` straight to its ready state, after `_init_values`.
- **Firmware updates.** Pin to tags. A patch that no longer applies stops
  the build; the scenario tests and sanitizers then check the new version.
