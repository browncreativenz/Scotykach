/**
 * @file ScotykachEngine.h
 * @brief Spotykach's firmware engine (src/core) run off the hardware.
 *
 * The firmware is compiled as is (plus patches/), against the desktop
 * stand-ins in shim/. This class plays the part of the module's
 * interrupts, in a fixed order, once per 96-sample block at 48 kHz:
 *
 *   TIM5 timer, 500 Hz       Core::prepare (T5Callback in app.cpp)
 *   audio callback           Driver::tick (the clock, from CoreUI::tick),
 *                            the CV inputs (CoreUI::read_cv), Core::process
 *   DAC callback, 48 kHz     Modulator::process x 96 (the two CV outputs)
 *
 * 96 samples at 48 kHz is 2 ms, exactly the timer's period, so each block
 * gets one of each, as on the module. process() accepts any host block
 * size: it buffers through a 96-sample FIFO and runs the firmware block on
 * every 96th sample, so output never depends on how the host slices the
 * audio. That costs latencySamples() = 96 samples (2 ms) on every path.
 *
 * Each Engine has its own memory pool (about 32 MB, allocated in the
 * constructor) and its own sample clock (shim/Context.h), so instances in
 * one process are independent. Not thread-safe: call everything for one
 * engine from one thread at a time.
 *
 * Always runs at 48 kHz, as the module does; the firmware's constants are
 * in samples at 48 kHz.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "Context.h"
#include "core/core.h"
#include "ui/speed.map.h"

namespace scotykach {

class Engine
{
  public:
    static constexpr int    kBlock      = 96;       // firmware block_size (app.cpp)
    static constexpr double kSampleRate = 48000.0;  // firmware sample_rate (app.cpp)

    /** Allocates the buffers and boots the engine with the module's factory
     *  settings (see applyFactorySettings). Not real-time safe. */
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    /** Stereo processing, any block size; in and out may alias. */
    void process(const float* inL, const float* inR, float* outL, float* outR, std::size_t n);

    /** Delay added by the 96-sample block FIFO. */
    static constexpr int latencySamples() { return kBlock; }

    /** Input samples still to arrive before the next firmware block runs
     *  (1..96). Anything done through control() now lands in that block. */
    std::size_t samplesToNextBlock() const { return kBlock - fifoPos; }

    /** Firmware blocks run so far. */
    uint64_t blocks() const { return blockCount; }

    /** Calls f(core) with this engine's context current. Every call into
     *  firmware code outside process() must go through here, so that it
     *  sees this engine's clock and memory. */
    template <typename F>
    decltype(auto) control(F&& f)
    {
        shim::Context::Scope scope(ctx);
        return f(*core);
    }

    /** The two modulator outputs (the module's CV outputs), 0..1, as of the
     *  last block. */
    float modOut(spotykach::Deck::Ref ref) const { return modValue[ref]; }

    /** The CV inputs, as calibrated readings (what CoreUI::read_cv gets from
     *  the calibrator). All zero is nothing patched. Read every block. */
    struct CvInputs
    {
        float mix    [spotykach::Deck::Count] = {};   // CV_MIX_A/B
        float posSize[spotykach::Deck::Count] = {};   // CV_SIZE_POS_A/B
        float voct   [spotykach::Deck::Count] = {};   // CV_V_OCT_A/B, in semitones
        float crossfade                       = 0.f;  // CV_CROSSFADE
    };
    CvInputs cv;

  private:
    void runBlock();
    void readCv();
    void applyFactorySettings();

    struct ZeroedDelete { void operator()(spotykach::Core*) const; };

    shim::Context                             ctx;
    std::unique_ptr<spotykach::SDRAMBuffer>   pool;
    std::unique_ptr<spotykach::Core, ZeroedDelete> core;

    float modValue[spotykach::Deck::Count] = {};
    spotykach::SpeedMap<60> speedMap;   // CoreUI's V/Oct to speed table

    /* Block FIFO: input collects here until a full firmware block is ready;
     * output plays back the previous block's result. */
    float       fifoInL [kBlock] = {};
    float       fifoInR [kBlock] = {};
    float       fifoOutL[kBlock] = {};
    float       fifoOutR[kBlock] = {};
    std::size_t fifoPos          = 0;
    uint64_t    blockCount       = 0;
};

} // namespace scotykach
