/**
 * @file ScotykachProcessor.cpp
 * @brief See ScotykachProcessor.h.
 */

#include "ScotykachProcessor.h"

#include <cmath>
#include <limits>

#include "ScotykachModule.h"

namespace scotykach {

using namespace spotykach;
using namespace params;

namespace {

using Pad = Hardware::Pad;

constexpr Pad kPads[] = {
    Pad::PlayA, Pad::RevA, Pad::GritA, Pad::FluxA, Pad::SeqA,
    Pad::PlayB, Pad::RevB, Pad::GritB, Pad::FluxB, Pad::SeqB,
    Pad::Spot,  Pad::Alt,
};
static_assert(sizeof(kPads) / sizeof(kPads[0]) == kTap, "a pad for every pad button");

constexpr Mode kModes[] = { Mode::Reel, Mode::Slice, Mode::Drift };

/* The order buttons are pressed in when several change in one block:
 * modifiers first, as a player holds Alt before tapping Play. Releases go
 * in the reverse order. */
constexpr Button kPressOrder[] = {
    kTap, kAlt, kGritA, kFluxA, kGritB, kFluxB,
    kPlayA, kRevA, kSeqA, kPlayB, kRevB, kSeqB, kSpot, kGateA, kGateB,
};
static_assert(sizeof(kPressOrder) / sizeof(kPressOrder[0]) == kButtons, "every button once");

constexpr int kTempo = GlobalControl((int)Panel::Global::Tempo);

constexpr double kClocksPerQuarter = 24.0;   // MIDI clock

/** Unset: forces the first block to apply the parameter. */
constexpr float kUnset = std::numeric_limits<float>::quiet_NaN();

} // namespace

Processor::Processor()
    : mod(std::make_unique<Module>())
{
    for (int p = 0; p < kCount; ++p)
    {
        values[p].store(Get(p).defaultValue);
        changed[p].store(false);
        // Pads, Tap and gates start released, as on the module; everything
        // else is applied on the first block.
        applied[p] = Get(p).kind == Kind::Button ? 0.f : kUnset;
    }
    mod->setBlockHook([this](uint64_t blockStart, Panel& panel) { feedClock(blockStart, panel); });
    prepare(48000.0);
}

Processor::~Processor() = default;

void Processor::prepare(double rate)
{
    hostRate = rate;
    bridge.Prepare((int)std::lround(rate), Module::latencySamples());

    // Host time is counted from here; the module's samples from the same
    // point, offset by the up-converter's delay.
    const int r = (int)std::lround(rate);
    if (r == RateBridge::kInnerRate) upDelay = 0.0;
    else
    {
        RationalResampler up;
        up.Prepare(r, RateBridge::kInnerRate);
        upDelay = up.GroupDelaySeconds() * rate;
    }
    hostSamples  = 0;
    moduleOrigin = mod->blocks() * Module::kBlock + (Module::kBlock - mod->samplesToNextBlock());
    hostClock    = HostClock::Off;   // locks again at the next bar line
}

void Processor::setParameter(int p, float value)
{
    if (p >= 0 && p < kCount) values[p].store(Clamp(p, value));
}

float Processor::getParameter(int p) const
{
    return p >= 0 && p < kCount ? values[p].load() : 0.f;
}

bool Processor::takeChanged(int p)
{
    return p >= 0 && p < kCount && changed[p].exchange(false);
}

// ── Parameters ──────────────────────────────────────────────────────────────

void Processor::applyButton(Panel& panel, int b, bool held)
{
    if (b < kTap)        panel.pad(kPads[b], held);
    else if (b == kTap)  panel.setTap(held);
    else                 panel.setGate(b == kGateA ? Deck::A : Deck::B, held);
    applied[ButtonParam((Button)b)] = held ? 1.f : 0.f;
}

void Processor::applyParameters(Panel& panel)
{
    for (int p = 0; p < kButtonBase; ++p)
    {
        const float v = values[p].load();
        if (v == applied[p]) continue;   // a NaN (unset) never compares equal

        if (p < kGlobalBase)
        {
            const int deck = (p - kDeckBase) / kDeckControls;
            const int c    = (p - kDeckBase) % kDeckControls;
            panel.set(deck == 0 ? Deck::A : Deck::B, (Panel::Control)c, v);
        }
        else if (p < kCvBase)      panel.set((Panel::Global)(p - kGlobalBase), v);
        else if (p < kSwitchBase)  panel.setCv((Hardware::CvInputId)(p - kCvBase), v);
        else if (p < kButtonBase)
        {
            const int i = (int)v;
            switch ((Switch)(p - kSwitchBase))
            {
                case kRoute:    panel.setRoute((Panel::Route)i); break;
                case kModeA:    panel.setMode(Deck::A, kModes[i]); break;
                case kModeB:    panel.setMode(Deck::B, kModes[i]); break;
                case kModTypeA: panel.setModType(Deck::A, (Panel::ModType)i); break;
                case kModTypeB: panel.setModType(Deck::B, (Panel::ModType)i); break;
                case kCvRouteA: panel.setCvRoute(Deck::A, (Panel::CvRoute)i); break;
                case kCvRouteB: panel.setCvRoute(Deck::B, (Panel::CvRoute)i); break;
                case kClock:    clockMode = i; break;
                case kSwitches: break;
            }
        }
        applied[p] = v;
    }

    for (Button b : kPressOrder)
        if (values[ButtonParam(b)].load() >= .5f && applied[ButtonParam(b)] < .5f)
            applyButton(panel, b, true);
    for (int i = kButtons - 1; i >= 0; --i)
    {
        const Button b = kPressOrder[i];
        if (values[ButtonParam(b)].load() < .5f && applied[ButtonParam(b)] >= .5f)
            applyButton(panel, b, false);
    }
}

void Processor::readBack(Panel& panel)
{
    // The firmware moves some values itself: tap tempo, MIDI CCs, Size
    // fitting the tempo, Pitch settling on a quantised step.
    for (int p = 0; p < kCvBase; ++p)
    {
        const float fw = p < kGlobalBase
            ? panel.get((p - kDeckBase) / kDeckControls == 0 ? Deck::A : Deck::B,
                        (Panel::Control)((p - kDeckBase) % kDeckControls))
            : panel.get((Panel::Global)(p - kGlobalBase));
        if (std::isnan(applied[p]) || std::abs(fw - applied[p]) < 1e-6f) continue;

        // Only if the host hasn't set a newer value meanwhile.
        float expected = applied[p];
        if (values[p].compare_exchange_strong(expected, fw)) changed[p].store(true);
        applied[p] = fw;
    }
}

// ── Clock ───────────────────────────────────────────────────────────────────

void Processor::beginTransport(Panel& panel, const Transport& t, int n)
{
    const bool following = clockMode == kClockLive && t.valid;
    const bool playing   = following && t.playing;

    if (playing)
    {
        if (hostClock == HostClock::Off) hostClock = HostClock::Waiting;
        else if (std::abs(t.ppq - expectedPpq) > 1.0 / (4 * kClocksPerQuarter))
            hostClock = HostClock::Waiting;   // the host jumped: lock again
        expectedPpq   = t.ppq + n * t.bpm / (60.0 * hostRate);
        lateTolerance = n * t.bpm / (60.0 * hostRate);   // one host block
    }
    else
    {
        if (hostClock == HostClock::Locked) panel.midiIn(0xFC);   // Stop
        hostClock = HostClock::Off;
    }

    // Until it locks, the module keeps time on its internal clock: at the
    // host's tempo when following, else at its own.
    if (hostClock != HostClock::Locked)
    {
        panel.setClockSource(Driver::Source::internal);
        if (following) panel.setInternalTempo((float)t.bpm);
        else if (wasFollowing && !std::isnan(applied[kTempo]))
            panel.set(Panel::Global::Tempo, applied[kTempo]);   // back to the module's own tempo
    }
    wasFollowing = following;
}

double Processor::hostTimeOf(uint64_t moduleSample) const
{
    // Host samples since prepare() of the input that became this module
    // sample.
    return ((double)moduleSample - (double)moduleOrigin) * hostRate / Module::kSampleRate - upDelay;
}

void Processor::feedClock(uint64_t blockStart, Panel& panel)
{
    if (hostClock == HostClock::Off) return;

    // This block's audio, in quarter notes on the host's timeline.
    const double perQ = transport.bpm / (60.0 * hostRate);   // quarters per host sample
    const double q0   = transport.ppq + (hostTimeOf(blockStart) - (double)hostSamples) * perQ;
    const double q1   = transport.ppq + (hostTimeOf(blockStart + Module::kBlock) - (double)hostSamples) * perQ;

    if (hostClock == HostClock::Waiting)
    {
        // Lock at the first bar line in or after this block. A host reports
        // its transport per block, so a bar line it started on can be up to
        // a block behind: one that passed that recently locks now (only the
        // first beat comes late; the clocks after it are on time).
        const double bar  = transport.beatsPerBar > 0 ? transport.beatsPerBar : 4.0;
        const double late = std::min(lateTolerance, bar / 16);
        const double k    = std::ceil((q0 - late - transport.barStartPpq) / bar - 1e-9);
        const double line = transport.barStartPpq + k * bar;
        if (line >= q1) return;
        panel.setClockSource(Driver::Source::internal);   // a clean restart, see the header
        panel.setClockSource(Driver::Source::midi);
        panel.midiIn(0xF8);                               // the bar's first clock
        lastClock = (int64_t)std::llround(line * kClocksPerQuarter);
        hostClock = HostClock::Locked;
        return;
    }

    // Every clock that falls in this block (one at most, at any sane tempo).
    for (int sent = 0; sent < 4 && (double)(lastClock + 1) < q1 * kClocksPerQuarter; ++sent)
    {
        panel.midiIn(0xF8);
        ++lastClock;
    }
}

// ── Processing ──────────────────────────────────────────────────────────────

void Processor::runSegment(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (n <= 0) return;
    bridge.Process(inL, inR, outL, outR, n,
                   [this](float* l, float* r, int m) { mod->process(l, r, l, r, (std::size_t)m); });
}

void Processor::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                        const Transport& t, const MidiMessage* midi, int numMidi)
{
    if (n <= 0) return;

    mod->control([&](Panel& panel, Core&) {
        applyParameters(panel);
        beginTransport(panel, t, n);
    });

    transport = t;

    // Split the block at each MIDI message, so it reaches the module at its
    // own sample (the bridge's output doesn't depend on how blocks split).
    int pos = 0;
    for (int i = 0; i < numMidi; ++i)
    {
        const int at = std::min(std::max(midi[i].offset, pos), n);
        runSegment(inL + pos, inR + pos, outL + pos, outR + pos, at - pos);
        pos = at;
        mod->control([&](Panel& panel, Core&) {
            panel.midiIn(midi[i].data[0], midi[i].data[1], midi[i].data[2]);
        });
    }
    runSegment(inL + pos, inR + pos, outL + pos, outR + pos, n - pos);

    mod->control([&](Panel& panel, Core&) { readBack(panel); });
    hostSamples += n;
}

} // namespace scotykach
