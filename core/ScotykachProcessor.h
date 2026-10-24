/**
 * @file ScotykachProcessor.h
 * @brief Everything the plugin needs, without JUCE: the module at any
 *        sample rate, its parameters, MIDI in, and Live's clock.
 *
 *   - Sample rate: the module runs at 48 kHz, as the hardware does;
 *     RateBridge converts other host rates around it. latencySamples() is
 *     the total delay to report to the host.
 *   - Parameters (ScotykachParams.h): set from any thread; applied at the
 *     start of each host block, and land at the next 96-sample module block.
 *     Buttons changing in the same block are pressed modifiers first (Tap,
 *     Alt, Grit, Flux, then the rest) and released in the reverse order. A
 *     press is held for at least one module block, so one shorter than a
 *     host block (a quick click) still reaches the firmware.
 *     When the firmware changes a value itself (tap tempo, a MIDI CC, Size
 *     fitting the tempo), the parameter follows and takeChanged() reports it.
 *   - MIDI: messages go to the module's USB MIDI port at their sample
 *     offset (the block is split there), so notes trigger slices on time.
 *     The firmware listens on channel 1 for deck A and 2 for deck B.
 *   - Clock "Live": while the host transport plays, the module runs on MIDI
 *     clock made from the host's beat position, 24 per quarter note, each in
 *     the module block where it falls. It locks at the first bar line, so
 *     the module's bars are the host's bars, and again after the host jumps;
 *     until then it keeps time on its internal clock at the host's tempo, as
 *     it does while the transport is stopped. Clock "Internal": the module's
 *     own clock and its Tempo control.
 *
 *     Locking restarts the module's clock by stepping its clock source
 *     round (Driver::toggle_source resets every count) and then sends the
 *     first clock. A MIDI Start would do the same reset, but Driver::reset()
 *     fakes a beat when the source changed under 4 ms before, which would
 *     put every beat a 16th early.
 *
 *   - The window: led() and deckStatus() report what the module's LEDs and
 *     decks showed at the end of the last process() call.
 *
 * Not thread-safe except setParameter/getParameter/takeChanged, led and
 * deckStatus: call the rest from the audio thread (or one thread).
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "RateBridge.h"
#include "ScotykachParams.h"

namespace spotykach { class Core; }

namespace scotykach {

class Module;
class Panel;

class Processor
{
  public:
    struct Transport
    {
        bool   valid       = false;   // the host reports a transport
        bool   playing     = false;
        double bpm         = 120.0;
        double ppq         = 0.0;     // quarter notes, at the block's first sample
        double barStartPpq = 0.0;     // where the current bar began
        double beatsPerBar = 4.0;     // in quarter notes (4/4 is 4, 6/8 is 3)
    };

    struct MidiMessage
    {
        int     offset;    // sample offset within the block
        uint8_t data[3];
    };

    /** Boots the module (about 32 MB). Not real-time safe. */
    Processor();
    ~Processor();

    /** Sets the host rate. Not real-time safe; the module and its loops
     *  carry over. */
    void prepare(double hostRate);

    /** Total delay through rate conversion and the module, host samples. */
    int latencySamples() const { return bridge.LatencySamples(); }

    void  setParameter(int p, float value);
    float getParameter(int p) const;
    /** True once each time the firmware has changed parameter p itself. */
    bool  takeChanged(int p);

    /** Stereo processing at the host rate; in and out may alias. MIDI
     *  messages must be in offset order. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                 const Transport& transport, const MidiMessage* midi = nullptr, int numMidi = 0);

    /** The module, for tests and the plugin window. */
    Module& module() { return *mod; }

    /** One LED as the module last showed it, brightness applied: 0xRRGGBB.
     *  Indexes are Hardware::LedId. Any thread. */
    static constexpr int kMaxLeds = 128;
    uint32_t led(int index) const;

    /** What a deck is doing, as its Play LED tells on the module. */
    enum class DeckState : uint8_t { Empty, Armed, Recording, Overdubbing, Playing, Stopped };
    struct DeckStatus
    {
        DeckState state   = DeckState::Empty;
        float     seconds = 0.f;   // the loop's length (0 while empty or armed)
    };
    /** Deck 0 is A, 1 is B. Any thread. */
    DeckStatus deckStatus(int deck) const;

  private:
    void applyParameters(Panel&);
    void applyButton(Panel&, int button, bool held);
    void readBack(Panel&);
    void publish(Panel&, spotykach::Core&);
    void beginTransport(Panel&, const Transport&, int n);
    double hostTimeOf(uint64_t moduleSample) const;
    void feedClock(uint64_t blockStart, Panel&);
    void runSegment(const float* inL, const float* inR, float* outL, float* outR, int n);

    std::unique_ptr<Module> mod;
    RateBridge              bridge;
    double                  hostRate = 48000.0;

    std::array<std::atomic<float>, params::kCount> values;
    std::array<std::atomic<bool>,  params::kCount> changed;
    std::array<float,              params::kCount> applied;

    // Presses counted as they are set, so a press and release between two
    // blocks still presses; heldUntil keeps it down for a module block.
    std::array<std::atomic<uint32_t>, params::kButtons> presses {};
    std::array<uint32_t,              params::kButtons> seenPresses {};
    std::array<uint64_t,              params::kButtons> heldUntil {};

    std::array<std::atomic<uint32_t>, kMaxLeds> leds {};
    std::atomic<uint8_t> deckState[2]   {};
    std::atomic<float>   deckSeconds[2] {};

    // Clock following the host (feedClock runs inside process()).
    enum class HostClock { Off, Waiting, Locked };
    Transport transport;
    int       clockMode    = params::kClockLive;
    HostClock hostClock    = HostClock::Off;
    bool      wasFollowing = false;   // clock Live, with a host transport
    int64_t   lastClock    = 0;       // the last 24 PPQN clock sent
    double    expectedPpq  = 0.0;     // where this call should start, from the last
    double    lateTolerance = 0.0;    // quarters a bar line may be behind and still lock
    int64_t   hostSamples  = 0;       // host samples since prepare(), before this call
    uint64_t  moduleOrigin = 0;       // module samples at prepare()
    double    upDelay      = 0.0;     // host samples from host input to module input
};

} // namespace scotykach
