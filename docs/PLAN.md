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
CMakeLists.txt              top level; fetches JUCE 8.0.16-security for the plugin
cmake/Firmware.cmake        patched copies of the firmware, DaisySP, the libraries
external/spotykach/         submodule, v1.3.1 (never edited)
external/DaisySP/           submodule, at the firmware's pinned commit
patches/portability/        make x86 behave as the module's Cortex-M7 does
patches/fixes/              firmware bugs a plugin's automation can reach
patches/instances/          let several modules share one process
patches/plugin/             the one hook the plugin needs into CoreUI
core/                       desktop side, plain C++17, no JUCE
  shim/                     libDaisy stand-ins (System and StopwatchTimer on the
                            sample clock, UiEventQueue, PotMonitor, MIDI types,
                            PersistentStorage, ...); prelude.h (force-included)
  shim/Context.h            per-module clock and memory pool, current per thread
  overlay/hw/               written over the firmware copy's src/hw: Hardware (the
                            panel as plain state), Card (no SD card), the memory pool
  ScotykachEngine           the engine alone (src/core), as in S0: for tests
  ScotykachModule           the whole firmware, as app.cpp's AppImpl runs it
  ScotykachPanel            pots, switches, pads, CV, gates, MIDI into the firmware
  ScotykachParams           the 91 plugin parameters: IDs, kinds, defaults
  ScotykachProcessor        host-facing: rate bridge, parameters, MIDI, Live's clock
  Resampler.h, RateBridge.h host rate <-> 48 kHz, copied from Granular-plugin
plugin/                     the JUCE plugin: parameters, transport, MIDI, state
tools/HostRig.h             drives a Processor as a host would
tools/render/               scotykach_render: the processor, scripted, WAV in and out
tests/                      engine, module, processor, parameter, resampler,
                            stress and plugin tests
.github/workflows/          CI: Linux GCC, Clang and sanitizers; macOS
```

Shim headers come first on the include path, so the firmware's
`<daisy_seed.h>` and the like resolve to the desktop versions. The
hardware layer (`src/hw`) is replaced whole instead: the build writes
`core/overlay/hw/*.h` over the copy's own files, so every include finds
the desktop version however it is spelled (`"../hw/hardware.h"` too).

## Firmware patches

At configure time the build copies the firmware's `src/` into the build
tree, applies the patches in order, and writes the hardware overlay over
`src/hw`. If a patch no longer applies (after a firmware update, say),
configuring stops and names it. Each changed line carries a
`Desktop (Scotykach)` comment.

| Patch | Why |
|---|---|
| `portability/0001-arm-modulo-by-zero` | In Slice mode with nothing recorded, `Generator::apply_dimensions` takes `% 0`. The Cortex-M7 doesn't trap on that (`x % 0` is `x`); x86 crashes. `arm_mod()` gives the module's result |
| `portability/0002-repeatable-panner` | The Generative Stereo panner draws from `std::random_device`, which reads OS entropy on a desktop, so no two renders were alike. A default-seeded `std::mt19937` makes renders and Live exports repeat |
| `portability/0003-arm-float-to-int` | Overdubbing at exactly 1x divides by zero in the write-head skip, and the empty-buffer playhead is 0/0. The module converts inf and NaN to integers by saturating (inf to 0xFFFFFFFF, NaN to 0); GCC and Clang on x86-64 each give other values, which took the other branch. `arm_f2u32()` and `arm_f2s32()` convert as the module does |
| `portability/0004-clang-array` | `Flux::switch_mode` initialises a variable-length array, a GCC extension Clang rejects. A `constexpr` count gives the same array |
| `portability/0005-int32-max` | `LEDRing` calls `std::max(int32_t, long)`, which compiles only where `int32_t` is `long`, as on the module's toolchain |
| `portability/0006-empty-slice-grid` | A Slice loop under two 16ths has no slices, and `_auto_cue_max_idx` wraps to the largest `size_t`: 0xFFFFFFFF on the module, 2^64 - 1 here, which made the next conversion undefined. Takes 32 bits and converts as the module |
| `portability/0007-phasor-saturating` | `Phasor`, the voices' fixed-point playhead, converts float to int counting on the module's saturating `VCVT` (its own comment says so); far-out positions were undefined here |
| `portability/0008-led-ring-saturating` | The LED ring converts ring positions to integers the same way; positions far outside the ring were undefined here |
| `portability/0009-mod-sync-index` | With Alt held, Mod Speed at 1 indexes the sync table at -1.0, which the module converts to 0 and x86 to an index past the table |
| `portability/0010-detector-digital-silence` | The detector that starts a Reel or Drift recording "at the next sound" opened on exact digital silence (DaisySP's `fastlog10f(0)` is about -0.9 dB, above the -40 dB threshold). The module's input always carries some noise; a DAW's silent track is exact zeros, so an armed deck started recording at once. Silence now keeps it waiting |
| `portability/0011-slice-size-empty-deck` | An empty deck in Slice mode (Clear makes one) has slices of length 0, so the Size conversions divide by zero and convert inf or NaN to integers, which the module saturates and x86 leaves undefined. Found by the stress test once Clear and Free Size existed |
| `fixes/0001-led-ring-long-segment` | A loop shorter than the shortest slice (1344 samples) makes the ring's segment several rings long; from 128 LEDs on, an `int8_t` loop counter can never reach the end and the main loop freezes. Stops at one ring, which lights the LEDs exactly as before |
| `fixes/0002-led-ring-negative-position` | The ring wraps negative positions by adding 1 until positive, which never ends once a position is so negative that adding 1 changes nothing (a voice in a loop a few samples long). Subtracting the floor gives the same result and ends |
| `fixes/0003-midi-cc-other-channels` | A CC on a channel neither deck listens on indexed `deck(2)`, past the two decks, and wrote into the memory after them. Deck CCs on other channels are ignored now; the crossfader, which belongs to no deck, still works on every channel |
| `instances/0001-per-instance-buffer-tables` | `Core::init` kept its buffer tables in function statics, so a second engine in the process wrote into the first one's loops (and `Detector` keeps the table's address). They become `Core` members |
| `instances/0002-ui-statics` | `CoreUI` kept six values in statics: the Tap switch's state and its throttle, the MIDI clock edge, and three LED animation counters. Shared, one instance's MIDI clocks reached another (the instance test catches it). They become `CoreUI` members |
| `plugin/0001-panel-access` | Declares `scotykach::Panel` a friend of `CoreUI`, so each parameter can set its value as `CoreUI::_on_midi_cc` does |
| `plugin/0002-plugin-switches` | The plugin's own controls for what the module does with Alt: Mod Sync and Free Size flags `CoreUI` reads alongside Alt; read-only Grit and Flux lock state; `Deck::cancel_record` (also drops a Slice recording queued for the next beat, which disarming leaves queued); and After Recording "Play", which stops recording where a closing loop starts to play. All off by default, so the module alone behaves as before |

The `fixes/` patches are firmware bugs, not desktop differences: the module
would freeze or corrupt memory the same way. All three need input no hand
gives a module (two record presses a few milliseconds apart, a CC on a
third channel), but a plugin's automation, MIDI or validator gives it
easily: `pluginval` hung on the first, and the stress test below found the
rest. They are worth reporting to Synthux.

One static is left shared, on purpose: `DeckStorage::process` keeps an SD
card error flag, which can never be set without a card. `Config::dynamic()`
(the SD card's `config.txt`) is also process-wide, but stays at its
defaults, since there is no card to read it from.

DaisySP and the firmware also build with `-fwrapv`: DaisySP's `WhiteNoise`
relies on signed overflow wrapping, as it does on the module.

`scotykach_fw_single` is the firmware with the portability and fix patches
only: the closest the desktop gets to the module. The `same_as_firmware_*` tests
render every scenario through it and through the full build (one per
process, since unpatched it supports only one engine) and require the same
bytes, so the instance patch provably changes nothing about the sound.

The sanitizer CI job (`-DSCOTYKACH_SANITIZE=ON`) runs every test under
AddressSanitizer and UndefinedBehaviorSanitizer, including out-of-range
float-to-integer conversions. That is how patches 0001 and 0003 were found,
and it fails on any new report. With S1's UI, storage and processor in, it
reports nothing.

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

### What S1 found

- **The whole firmware matches the engine alone.** Driven through the
  panel, the module (engine, UI, storage) gives exactly the samples the S0
  engine gives for the same performance, in every mode and route
  (`tests/module_test.cpp`). The UI adds behaviour, not sound.
- **Switch order.** When the route and mode switches change at once,
  `CoreUI::_process_switches` applies the route first; the Generative
  Stereo panner then sets up differently than the other way round.
- **Locking to Live's clock.** `Driver::reset()`, which MIDI Start calls,
  fakes a beat if the clock source changed less than 4 ms before, and
  then every beat lands a 16th early. The processor therefore locks by
  stepping the clock source round (a forced reset) and sending the first
  clock, instead of sending Start. See Clock sync below.
- **No SD card, no settings.** With the desktop `Card` reporting no card,
  `Storage` stays idle, as on a module without one, and the Tap+Play storage
  gestures do nothing. `Settings` starts from the factory calibration.
- **Boot.** The LED start-up animation (about 3.3 s at the module's 125 Hz
  LED rate) is run to its end when the module is created, then CoreUI takes
  its factory values, so the plugin is playable at once.

### What S2 found

- **Randomised input finds what playing doesn't.** `pluginval`'s
  parameter thread-safety test sets every parameter at random before each
  block (pads and switches included) and hung on the LED ring. The new
  stress test does the same with random MIDI and a jumping transport; under
  the sanitizers it found the other two fixes and four more ARM-versus-x86
  conversions (0006 to 0009). After them, 240 seeds run clean, and CI runs
  120 under the sanitizers on every push.
- **The plugin passes `pluginval` at its strictest level (10)**, editor
  tests included, on Linux (CI runs it there and on macOS, with `auval`).

### What the first try in Live found

- **On/off switches don't make pads.** The generic window shows the pads
  as switches, and a pad acts only when it goes on, so "Alt on, Play on,
  then Play off and Alt off" armed the deck and never closed the loop; it
  recorded until the deck was full (42 s) and closed it there. The window
  now has the module's pads, pressed while the mouse is down, with Alt
  latching, each lit with its LED and each deck's state written under it.
- **A click shorter than a host block** used to be lost between blocks.
  The processor now counts presses as they are set and holds each for at
  least one module block (the firmware reads Tap and the gates once per
  block); the plugin passes button changes on as they happen.
- **Closing a loop starts overdubbing**, which fades the loop by A
  Feedback (0.95, -0.15 dB a pass) when nothing comes in: a third Alt +
  Play stops the overdub. This is the module's own behaviour; the window
  and README now say so.
- **The recording detector opened on digital silence** (portability
  0010).
- **No Alt in the window.** The plugin has no button shortage, so each
  thing the module does with Alt held has its own button or switch:
  Record (one button stepping through record, close the loop, overdub,
  stop overdubbing; it cancels while armed), Clear, Seq Rec, Seq Clear,
  Record From (input or the deck's own output), Grit and Flux lock,
  Quantise, Mod Sync and Free Size. **After Recording** defaults to *Play*:
  a closed loop plays without overdubbing (*Overdub* is the module's way).
  The Alt and Spot parameters stay, for mappings, but not in the window,
  and the parameter list below the buttons shows only knobs and settings.

### Timing: one firmware "tick" per 96 samples

On the hardware, several things run at different rates:

| Hardware | Rate | In the plugin |
|---|---|---|
| TIM5 interrupt: `process_gate_in`, `Core::prepare`; LEDs every 4th call | 500 Hz = every 2 ms | once per block, since 96 samples at 48 kHz is exactly 2 ms |
| Audio callback: `ProcessAnalogControls`, `CoreUI::tick` (MIDI, then `Driver::tick`: the clock), `read_cv`, `Core::process` | every 96 samples | the same, on each full 96-sample block |
| Main loop: `CoreUI::process` (pads, switches, values), `Storage::process` | as fast as it can | once per block, first |
| DAC callback: `Modulator::process`, the two CV outputs | 48 kHz | 96 times per block |

Everything runs in that order on the audio thread, one block at a time.
That removes the races between interrupts and the main loop, and makes
renders repeatable. The shim's `System::GetNow()`, `GetTick()` and
`StopwatchTimer` count engine samples instead of wall-clock time. Hold
times, tap tempo and the clock timeout then behave the same in an offline
export as in real time; the tests check hold-to-clear (1.5 s) and tap
tempo.

A 96-sample FIFO decouples the module from host block sizes, as Capicola's
64-sample FIFO does. The latency reported to Live is 96 samples at 48 kHz
(2 ms), 217 at 44.1 kHz, 413 at 88.2 kHz and 449 at 96 kHz.

### Clock sync with Live

The Clock parameter is **Live** (the default) or **Internal**.

- **Live, transport playing:** the module runs on its MIDI clock input
  (`Driver`'s 24 PPQN mode). For every 2 ms module block the processor works
  out where that block's audio sits on Live's timeline (the host's beat
  position and tempo, through the sample-rate ratio and the up-converter's
  delay) and delivers each 24th of a beat in the block where it falls,
  which is the module's own clock resolution.
- **Locking:** the module locks at the first bar line after Live starts,
  so its bars are Live's bars (a bar line up to one host block behind still
  counts, since hosts report the transport per block), and again after Live
  jumps (a loop brace, a click in the arrangement). Until then it keeps time
  on its internal clock at Live's tempo.
- **Live, transport stopped:** the internal clock at Live's tempo, so Slice
  mode still records and loops in time.
- **Internal:** the module's own clock, Tempo control and Tap.

Measured with a simulated transport: a Slice recording started and stopped
off the beat is exactly four beats long at 100 and 120 BPM, and its
playback starts 2.4 ms after a host beat (the 2 ms FIFO and the slice's
fade-in). After a jump, the next loop is on the new grid. Live's own MIDI
Start and Stop are not needed; Stop is still sent when Live stops, as the
module would get it.

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
| Switches | Route (Stereo, Double Mono, Generative Stereo, shared); deck mode (Reel, Slice, Drift); mod type (A: Follow, LFO square, LFO random; B: Follow, LFO saw, LFO sine); Size/Pos CV route (Position, Both, Size); and the plugin's Clock (Live, Internal) |
| CV inputs | Size/Pos, Mix, V/Oct; Crossfade (shared): automatable, like Capicola's Mod In |
| Pads and buttons | Play, Rev, Grit, Flux, Seq per deck; Spot; Alt; Tap; the two gate inputs: button parameters, to MIDI-map in Live |

**Decided: each layered control is its own parameter** (SOS Feedback,
Grit Mix, Tempo and so on), so each automates and MIDI-maps on its own in
Live. `Panel` sets each one as the firmware's MIDI CC handler does
(`MValue::set`), so there is no pick-up and nothing jumps. Two pot gestures
keep their meaning when their button is held: Size in Slice mode with Tap
held fits the tempo to the loop, and Pitch with Alt held quantises.

The 91 parameters (`core/ScotykachParams.cpp`, `scotykach_render --list`):

| Group | Count | IDs |
|---|---|---|
| Deck controls, 0..1 | 2 × 17 | `a_pos`, `a_pos_offset`, `a_size`, `a_pitch`, `a_env`, `a_mix`, `a_feedback`, `a_window`, `a_env_size`, `a_mod_speed`, `a_mod_amount`, `a_grit_amount`, `a_grit_mix`, `a_grit_char`, `a_flux_time`, `a_flux_mix`, `a_flux_fb`; likewise `b_` |
| Shared controls, 0..1 | 6 | `crossfade`, `tempo`, `key_interval`, `click`, `pan_speed`, `pan_range` |
| CV inputs, -1..1 (±5 V) | 7 | `cv_a_size_pos`, `cv_a_mix`, `cv_a_voct`, `cv_crossfade`, `cv_b_...` |
| Switches | 8 | `route`, `a_mode`, `b_mode`, `a_mod_type`, `b_mod_type`, `a_cv_route`, `b_cv_route`, `clock` |
| The plugin's switches, for what the module does with Alt | 13 | `after_rec` (Play / Overdub), `a_rec_source` (Input / Deck), `a_grit_lock`, `a_flux_lock`, `a_quantise`, `a_mod_sync`, `a_free_size`; likewise `b_` |
| Buttons | 15 | the 12 pads (`a_play` ... `alt`), `tap`, `a_gate`, `b_gate` |
| Actions | 8 | `a_record`, `a_clear`, `a_seq_rec`, `a_seq_clear`; likewise `b_`. Each acts once when set to 1 and goes back to 0 itself, so a key mapped in Live acts on every press |

Defaults are the module's factory values, checked against a booted module
(`tests/params_test.cpp`); the decks start in Reel. When the firmware moves
a value itself (tap tempo, a MIDI CC, Size fitting the tempo, Pitch settling
on a quantised step), the parameter follows and the processor reports it,
so S2 can tell Live.

Buttons that change in the same host block are pressed modifiers first
(Tap, Alt, Grit, Flux, then the rest) and released in the reverse order,
so Alt and Play arriving together record, as Alt then Play does on the
module.

MIDI into the plugin works as on the module: `CoreMIDI` handles its note map
and its CC map (`config.h`: CC 3 crossfade, 14/15 record, 20–28 deck
controls, 85/86 play forward/reverse, 102–109 FX), on channel 1 for deck A
and 2 for deck B. A note triggers the deck at its pitch (middle C is 1x).
Messages reach the module at their own sample: the processor splits the
host block there.

## Milestones

| # | Milestone | Where | Done when |
|---|---|---|---|
| S0 ✅ | Submodules; patches; `shim/` with heap pool and sample clock; firmware `src/core` built into `scotykach_fw`; `ScotykachEngine`; `scotykach_render` with scripted actions; CTest scenarios; CI on Linux (GCC, Clang, sanitizers) and macOS | Cloud session | Reel, Slice and Drift record and loop; output is identical whatever the host block size; engines are independent in one process; the instance patch changes no output; no sanitizer reports |
| S1 ✅ | Desktop `Hardware` (overlay); firmware `CoreUI`, `CoreMIDI`, `Storage` and `Settings` running, with patches for the UI's statics and the panel hook; `ScotykachModule`, `ScotykachPanel`, the parameter table; `ScotykachProcessor` with rate bridge, MIDI at sample offsets and Live clock sync; the renderer on the processor | Cloud session | The module matches the engine alone; gestures work (Alt+Play record, hold to clear, Grit hold and lock, tap tempo, Pitch quantise, Size fits tempo); Slice locks to a simulated host transport; 44.1–96 kHz; block-size independent; instances independent; no sanitizer reports |
| S2 ✅ | JUCE plugin (VST3, AU, Standalone) with a window of both decks' buttons and switches (no Alt; the module's LEDs, rings and deck state) above a list of the knobs and settings; the 91 parameters, with firmware changes sent back to the host; the host's playhead as the transport; MIDI in at its sample; state with both loops at full quality; latency-matched bypass; the stress test and three firmware fixes; CI with `auval` and `pluginval` on macOS, `pluginval` on Linux | Cloud session; your Mac for Live | `pluginval` strictness 10 and `auval` pass; plugin tests pass (record with the pads, the clock on the host's tempo, MIDI CC back to the host, state round trip with the loop, mono input, bypass). In Live: still to try on your Mac |
| S3 | Gate outs, LFO outs and clock out as MIDI on a "Scotykach 1" virtual port, using the MIDI-out code copied from Capicola | Cloud plus your Mac | Gate notes play a Drum Rack; LFO CCs MIDI-map in Live |
| S4 | The plugin window, from a mockup you approve first: 12 pads, two 32-LED rings drawn from the firmware's own LED output, pots, switches, crossfader | Cloud session builds it and renders screenshots; your Mac for feel | Looks like the mockup, and the LEDs match the module |
| S5 (optional) | WAV import and export of loops (the firmware has `memory/wav.cpp`), drag and drop with Live; SD-card slots as loop banks | Cloud plus your Mac | A Live clip dropped on a deck plays; a deck loop drags out as a clip |

## Risks and open questions

- **Set size.** Loops are saved at full quality (decided): each deck holds
  up to 42 s of stereo audio, about 16 MB as floats, so a Live set grows by
  up to about 32 MB per instance, by less for shorter loops (a 2 s loop is
  0.73 MB). How Live copes with the biggest is to check on your Mac.
- **Saving while overdubbing.** The plugin copies the loops for Live's save
  while the audio thread may still be overdubbing them, so a save mid-pass
  can catch a block of that pass. Stopping the audio for the copy would be
  heard; loading, by contrast, pauses processing for the moment it takes.
- **Pads and a mouse.** Many gestures press two pads at once: Alt plus a
  pad, holding Play, Seq combinations. With a mouse that means a latching Alt
  and Shift in the window. A pad controller MIDI-mapped to the button
  parameters allows true chords; a press and release inside one host block
  (under about 10 ms) would be lost, which no hand manages.
- **CPU.** A 60 s render with both decks playing, Grit on and resampling at
  44.1 kHz took 3.4 s on a cloud x86 core: about 5.5 % of one core. To
  measure in Live, at a small buffer.
- **Random sequences differ by platform.** Drift's grain positions use
  `std::default_random_engine` and `std::normal_distribution`, which
  libstdc++ (Linux, and the module's toolchain) and libc++ (macOS)
  implement differently. Each platform is repeatable and the grains sound
  the same, but the exact sequence on a Mac is not the module's.
- **Memory.** Two 42 s stereo buffers, about 32 MB, plus delay lines, per
  instance (the firmware's third, for undo, is unused in v1.3.1). They are
  allocated when the engine is created, never on the audio thread.
- **Calibration and boot.** Handled (see What S1 found): factory
  calibration, and the start-up animation skipped.
- **Firmware updates.** Pin to tags. A patch that no longer applies stops
  the build; the scenario tests and sanitizers then check the new version.
