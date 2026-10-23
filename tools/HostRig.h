/**
 * @file HostRig.h
 * @brief Drives a Processor as a host would, for the renderer and tests.
 *
 * Renders the test signal (muted from a given time) at any host rate and
 * block size, applying timed steps: parameter changes, pad presses, MIDI
 * messages. A block is split at each step, so a step lands at the same
 * sample whatever the block size. A transport function gives the host's
 * transport for each block.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "ScotykachModule.h"
#include "ScotykachParams.h"
#include "ScotykachProcessor.h"
#include "TestSignal.h"

namespace scotykach::rig {

using Midi = std::vector<Processor::MidiMessage>;

struct ProcStep
{
    double                                time;   // seconds
    std::function<void(Processor&, Midi&)> act;   // MIDI pushed here goes out at this sample
};

using TransportAt = std::function<Processor::Transport(double seconds)>;

/** No transport: as in a host that doesn't report one. */
inline Processor::Transport NoTransport(double) { return {}; }

/** A transport that starts playing at `start` seconds from beat 0, 4/4. */
inline TransportAt PlayingFrom(double start, double bpm)
{
    return [=](double t) {
        Processor::Transport tr;
        tr.valid       = true;
        tr.bpm         = bpm;
        tr.playing     = t >= start;
        tr.ppq         = tr.playing ? (t - start) * bpm / 60.0 : 0.0;
        tr.beatsPerBar = 4.0;
        tr.barStartPpq = std::floor(tr.ppq / 4.0) * 4.0;
        return tr;
    };
}

struct ProcRender
{
    std::vector<float> l, r;
};

/** A step that sets parameter p. */
inline ProcStep Set(double time, int p, float v)
{
    return { time, [=](Processor& proc, Midi&) { proc.setParameter(p, v); } };
}

/** Press and release a button, `hold` seconds apart. */
inline std::vector<ProcStep> Tap(double time, params::Button b, double hold = 0.02)
{
    return { Set(time, params::ButtonParam(b), 1.f), Set(time + hold, params::ButtonParam(b), 0.f) };
}

/** Record on deck A the module's way: Alt held, tap Play. */
inline std::vector<ProcStep> RecordA(double time)
{
    std::vector<ProcStep> s = Tap(time, params::kAlt, 0.06);
    for (auto& x : Tap(time + 0.02, params::kPlayA)) s.push_back(x);
    return s;
}

/** A MIDI message at a time. */
inline ProcStep MidiAt(double time, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
    return { time, [=](Processor&, Midi& m) { m.push_back({ 0, { status, d1, d2 } }); } };
}

/** Plays steps against a processor, a block at a time, so tests can
 *  interleave several. */
class ProcPlayer
{
  public:
    ProcPlayer(Processor& p, double rate, double seconds, std::vector<ProcStep> s,
               TransportAt transportAt = NoTransport, double muteFrom = 1e9)
        : proc(p), rate(rate), muteFrom(muteFrom), steps(std::move(s)), transportAt(std::move(transportAt))
    {
        proc.prepare(rate);
        n = (std::size_t)(seconds * rate);
        inL.resize(n);
        inR.resize(n);
        TestSignal sig((float)rate);
        for (std::size_t i = 0; i < n; ++i)
        {
            sig.Next(inL[i], inR[i]);
            if ((double)i >= muteFrom * rate) inL[i] = inR[i] = 0.f;
        }
        out.l.resize(n);
        out.r.resize(n);
        std::stable_sort(steps.begin(), steps.end(),
                         [](const ProcStep& a, const ProcStep& b) { return a.time < b.time; });
    }

    bool done() const { return pos >= n; }

    /** Plays this audio instead of the test signal (cut or padded with
     *  silence to the length, muted from muteFrom). Call before advancing. */
    void setInput(const std::vector<float>& l, const std::vector<float>& r)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const bool live = i < l.size() && (double)i < muteFrom * rate;
            inL[i] = live ? l[i] : 0.f;
            inR[i] = live && i < r.size() ? r[i] : 0.f;
        }
    }

    /** Renders up to `block` samples (less if a step falls sooner). */
    void advance(std::size_t block)
    {
        midi.clear();
        for (; next < steps.size() && At(steps[next].time) <= pos; ++next) steps[next].act(proc, midi);
        std::size_t len = std::min(block, n - pos);
        if (next < steps.size()) len = std::min(len, At(steps[next].time) - pos);
        proc.process(&inL[pos], &inR[pos], &out.l[pos], &out.r[pos], (int)len,
                     transportAt((double)pos / rate), midi.data(), (int)midi.size());
        pos += len;
    }

    ProcRender out;

  private:
    std::size_t At(double t) const { return (std::size_t)std::llround(t * rate); }

    Processor&            proc;
    double                rate;
    double                muteFrom;
    std::vector<ProcStep> steps;
    TransportAt           transportAt;
    std::vector<float>    inL, inR;
    std::size_t           n = 0, pos = 0, next = 0;
    Midi                  midi;
};

inline ProcRender RunProcessor(Processor& proc, double rate, double seconds, std::size_t block,
                               std::vector<ProcStep> steps, TransportAt transportAt = NoTransport,
                               double muteFrom = 1e9)
{
    ProcPlayer player(proc, rate, seconds, std::move(steps), std::move(transportAt), muteFrom);
    while (!player.done()) player.advance(block);
    return std::move(player.out);
}

inline double Rms(const std::vector<float>& x, double rate, double from, double to)
{
    const std::size_t a = (std::size_t)(from * rate), b = std::min(x.size(), (std::size_t)(to * rate));
    double s = 0;
    for (std::size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
    return b > a ? std::sqrt(s / (double)(b - a)) : 0.0;
}

} // namespace scotykach::rig
