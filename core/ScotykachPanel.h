/**
 * @file ScotykachPanel.h
 * @brief The module's panel, as the plugin drives it.
 *
 * Everything a player can do to the module, routed into the firmware the
 * way the module routes it, except for the pots:
 *
 *   - Each pot control is its own parameter, including the ones the module
 *     reaches by holding Alt, Grit, Flux or Tap while turning a pot. A
 *     parameter sets its CoreUI value as the firmware's own MIDI CC handler
 *     (CoreUI::_on_midi_cc) does, so there is no pick-up and nothing jumps.
 *     Two pot gestures keep their meaning: Size in Slice mode with Tap held
 *     fits the tempo to the loop, and Pitch with Alt held quantises.
 *   - Switches become the shift-register bits CoreUI::_process_switches
 *     decodes; Tap is one of them.
 *   - Pads, CV inputs, gate and clock inputs and incoming MIDI go to the
 *     desktop Hardware, which the firmware polls.
 *
 * Panel is the one class the firmware's CoreUI lets in (patches/plugin).
 * Use it through ScotykachModule, which makes the module's context current;
 * changes take effect at the next 96-sample block, as on the module.
 */

#pragma once

#include <cstdint>

#include "core/core.h"
#include "hw/hardware.h"

namespace spotykach { class CoreUI; }

namespace scotykach {

class Panel
{
  public:
    using Deck = spotykach::Deck;
    using Pad  = spotykach::Hardware::Pad;

    /** The per-deck pot controls, one parameter each. */
    enum class Control : uint8_t
    {
        Position,       // Pos
        PositionOffset, // Pos with Alt (once the deck has cue points)
        Size,           // Size
        Pitch,          // Pitch
        Envelope,       // Env
        Mix,            // SOS: in/out mix
        Feedback,       // SOS with Alt: overdub feedback
        Window,         // Size with Alt in Drift
        EnvelopeSize,   // Env with Alt in Drift
        ModSpeed,       // Mod Freq
        ModAmount,      // Mod Amount
        GritIntensity,  // Pitch with Grit held
        GritMix,        // SOS with Grit held
        GritCharacter,  // Pos with Grit held
        FluxTime,       // Pitch with Flux held
        FluxMix,        // SOS with Flux held
        FluxFeedback,   // Pos with Flux held
        Count
    };

    /** The shared pot controls. */
    enum class Global : uint8_t
    {
        Crossfade,      // the crossfader
        Tempo,          // Mod Freq A with Tap held
        KeyInterval,    // SOS A with Tap held
        Click,          // Mod Amount A with Tap held: metronome level
        PanSpeed,       // Mod Freq B with Tap held
        PanRange,       // Mod Amount B with Tap held
        Count
    };

    enum class Route : uint8_t   { Stereo, DoubleMono, GenerativeStereo };
    /** Deck A: Follow, LFO square, LFO random. Deck B: Follow, LFO saw, LFO sine. */
    enum class ModType : uint8_t { Follow, Lfo1, Lfo2 };
    /** The Size/Pos switch: what the deck's Size/Pos CV input modulates. */
    enum class CvRoute : uint8_t { Position, Both, Size };

    Panel(spotykach::CoreUI&, spotykach::Core&, spotykach::Hardware&);

    void  set(Deck::Ref, Control, float value);   // 0..1
    float get(Deck::Ref, Control) const;
    void  set(Global, float value);               // 0..1
    float get(Global) const;

    void setRoute(Route);
    void setMode(Deck::Ref, spotykach::Mode);     // Reel, Slice or Drift
    void setModType(Deck::Ref, ModType);
    void setCvRoute(Deck::Ref, CvRoute);
    void setTap(bool held);

    /** A pad touch (true) or release (false). */
    void pad(Pad, bool touched);

    void setCv(spotykach::Hardware::CvInputId, float value);   // -1..1 (±5 V)
    void setGate(Deck::Ref, bool high);
    void setClockIn(bool high);

    /** A MIDI message as it arrives on the module's USB port. Returns false
     *  for anything the firmware doesn't read, or if the port is full. */
    bool midiIn(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0);

    /** Internal, the clock input (4 PPQN), or MIDI clock. */
    void setClockSource(spotykach::Driver::Source);

    /** Sets the internal clock's tempo directly, leaving the Tempo value
     *  alone (for following the host's tempo while its transport is
     *  stopped). Held to the firmware's 20..250 BPM. */
    void setInternalTempo(float bpm);

    /** Runs the LED start-up animation to its end, so CoreUI takes its
     *  factory values at the next block instead of after about 3 s. */
    void finishBoot();
    bool isBooted() const;

  private:
    spotykach::CoreUI&   ui;
    spotykach::Core&     core;
    spotykach::Hardware& hw;

    uint8_t switches[2] = {};   // closed switches; Hardware reads the inverse
    float   crossfade   = 0.f;  // Core keeps no readable copy

    void writeSwitches();
    void setSwitch(int reg, int bit, bool closed);
};

} // namespace scotykach
