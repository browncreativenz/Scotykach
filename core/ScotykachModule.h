/**
 * @file ScotykachModule.h
 * @brief The whole Spotykach firmware, engine and UI, run off the hardware.
 *
 * The desktop counterpart of the firmware's AppImpl (app.cpp): it owns the
 * same objects (Core, CoreUI, Hardware, Settings, Storage), boots them as
 * AppImpl::Init does, and plays the module's main loop and interrupts in a
 * fixed order once per 96-sample block at 48 kHz:
 *
 *   main loop                CoreUI::process (pads, switches, values),
 *                            Storage::process
 *   TIM5 timer, 500 Hz       CoreUI::process_gate_in, Core::prepare,
 *                            and every 4th time CoreUI::render_leds
 *   audio callback           CoreUI::tick (MIDI, then the clock),
 *                            CoreUI::read_cv, Core::process
 *   DAC callback, 48 kHz     the two modulators, 96 times
 *
 * 96 samples at 48 kHz is 2 ms, the timer's period. process() accepts any
 * block size through a 96-sample FIFO (latencySamples()), so output never
 * depends on how the host slices the audio.
 *
 * The hardware is the desktop Hardware (core/overlay/hw/hardware.h): no SD
 * card, so Storage stays idle, and no pots: each pot control is a parameter
 * on the Panel. The LED start-up animation is run to its end in the
 * constructor, so the module is playable at once.
 *
 * Each Module has its own memory pool (about 32 MB) and sample clock, so
 * instances in one process are independent. Not thread-safe: call
 * everything for one module from one thread at a time.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "Context.h"
#include "ScotykachPanel.h"

namespace scotykach {

class Module
{
  public:
    static constexpr int    kBlock      = 96;
    static constexpr double kSampleRate = 48000.0;

    /** Allocates and boots. Not real-time safe. */
    Module();
    ~Module();

    Module(const Module&)            = delete;
    Module& operator=(const Module&) = delete;

    /** Stereo processing, any block size; in and out may alias. */
    void process(const float* inL, const float* inR, float* outL, float* outR, std::size_t n);

    /** Delay added by the 96-sample block FIFO. */
    static constexpr int latencySamples() { return kBlock; }

    /** Input samples still to arrive before the next block runs (1..96). */
    std::size_t samplesToNextBlock() const { return kBlock - fifoPos; }

    /** Blocks run so far; engine time is blocks() * kBlock samples. */
    uint64_t blocks() const { return blockCount; }

    /** Calls f(panel, core) with this module's context current. Every call
     *  into firmware code outside process() goes through here. Changes land
     *  at the next block. */
    template <typename F>
    decltype(auto) control(F&& f)
    {
        shim::Context::Scope scope(ctx);
        return f(*panel, core());
    }

    /** Called at the start of each block, before the firmware runs, with the
     *  block's engine time in samples, inside the module's context. The
     *  processor uses it to deliver MIDI clock at the right block. */
    using BlockHook = std::function<void(uint64_t blockStart, Panel&)>;
    void setBlockHook(BlockHook h) { hook = std::move(h); }

    /** The panel's outputs, as of the last block. */
    const spotykach::Hardware& hardware() const;

    /** The two modulator outputs (the module's CV outputs), 0..1. */
    float modOut(spotykach::Deck::Ref ref) const { return modValue[ref]; }

  private:
    struct Firmware;
    struct FirmwareDelete { void operator()(Firmware*) const; };

    spotykach::Core& core();
    void runBlock();

    shim::Context                            ctx;
    std::unique_ptr<spotykach::SDRAMBuffer>  pool;
    std::unique_ptr<Firmware, FirmwareDelete> fw;
    std::unique_ptr<Panel>                   panel;
    BlockHook                                hook;

    float modValue[spotykach::Deck::Count] = {};
    int   timerCount = 0;   // TIM5 calls, for the LED refresh

    float       fifoInL [kBlock] = {};
    float       fifoInR [kBlock] = {};
    float       fifoOutL[kBlock] = {};
    float       fifoOutR[kBlock] = {};
    std::size_t fifoPos          = 0;
    uint64_t    blockCount       = 0;
};

} // namespace scotykach
