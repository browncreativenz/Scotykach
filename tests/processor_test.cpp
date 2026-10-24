/**
 * @file processor_test.cpp
 * @brief The host-facing processor: the module at any sample rate, output
 *        independent of block size, the clock locked to a host transport,
 *        and parameters the firmware moves itself.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Check.h"
#include "HostRig.h"

using namespace scotykach;
using namespace scotykach::test;
using namespace scotykach::rig;
using namespace scotykach::params;
using spotykach::Core;
using spotykach::Deck;

namespace {

constexpr double kFs = 48000.0;

float TempoNorm(double bpm) { return (float)((bpm - 20.0) / (250.0 - 20.0)); }

struct DeckState { std::size_t recSize; bool playing; bool armed; bool trackArmed; float tempo; };

DeckState StateOf(Processor& p, Deck::Ref ref = Deck::A)
{
    return p.module().control([&](Panel&, Core& c) {
        auto& d = c.deck(ref);
        return DeckState { d.buffer().rec_size(), d.is_playing(), d.is_armed(), d.track().is_armed(), c.driver().tempo() };
    });
}

std::vector<ProcStep> Join(std::initializer_list<std::vector<ProcStep>> parts)
{
    std::vector<ProcStep> all;
    for (const auto& p : parts) all.insert(all.end(), p.begin(), p.end());
    return all;
}

/** Record on deck A over MIDI (CC 14 on channel 1, the firmware's RecExt). */
ProcStep RecordMidi(double t) { return MidiAt(t, 0xB0, 14, 127); }

/** The first sample at or after `from` louder than the threshold. */
double Onset(const std::vector<float>& x, double rate, double from)
{
    std::size_t i = (std::size_t)(from * rate);
    while (i < x.size() && std::abs(x[i]) < 1e-4f) ++i;
    return (double)i / rate;
}

/** Reel: record two seconds with the pads, then the loop plays on, at every
 *  rate Live offers. */
void TestLoopAtRates()
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        Processor p;
        auto steps = Join({ RecordA(0.5), RecordA(2.5),
                            { Set(0, DeckControl(0, (int)Panel::Control::Mix), 1.f),
                              Set(0, GlobalControl((int)Panel::Global::Crossfade), 0.f) } });
        const auto r  = RunProcessor(p, rate, 6.0, 512, steps, NoTransport, 2.5);
        const auto st = StateOf(p);
        std::printf("  %6.0f Hz: latency %d, loop %.3f s, rms %.3f\n", rate, p.latencySamples(),
                    st.recSize / kFs, Rms(r.l, rate, 3.5, 6.0));
        CHECK(st.playing, "%.0f Hz: deck A should be playing", rate);
        CHECK(st.recSize > 1.8 * kFs && st.recSize <= 2.0 * kFs + 192, "%.0f Hz: loop %zu", rate, st.recSize);
        CHECK(Rms(r.l, rate, 3.5, 6.0) > 0.05, "%.0f Hz: the loop should be audible", rate);
        if (rate == 48000.0) CHECK(p.latencySamples() == Module::latencySamples(), "latency %d", p.latencySamples());
        else                 CHECK(p.latencySamples() > Module::latencySamples() * rate / kFs, "latency %d", p.latencySamples());
    }
}

/** The same performance in any host block size gives the same samples,
 *  MIDI included. */
void TestBlockSizes()
{
    auto steps = Join({ RecordA(0.5), RecordA(2.5),
                        { Set(0, SwitchParam(kModeA), 1.f),   // Slice
                          Set(0, DeckControl(0, (int)Panel::Control::Mix), 1.f),
                          MidiAt(3.3, 0x90, 64, 100), MidiAt(3.71, 0x90, 55, 100),
                          MidiAt(4.05, 0xB0, 24, 90) } });   // Pitch CC
    steps.push_back({ 0.1, [](Processor&, Midi&) {} });   // hosts start the transport on a block
    Processor ref;
    const auto a = RunProcessor(ref, 44100.0, 5.0, 256, steps, PlayingFrom(0.1, 123.0), 2.5);
    for (std::size_t block : { 7, 64, 1000, 4096 })
    {
        Processor p;
        const auto b = RunProcessor(p, 44100.0, 5.0, block, steps, PlayingFrom(0.1, 123.0), 2.5);
        std::size_t diff = a.l.size();
        for (std::size_t i = 0; i < a.l.size(); ++i)
            if (a.l[i] != b.l[i] || a.r[i] != b.r[i]) { diff = i; break; }
        CHECK(diff == a.l.size(), "%zu-sample blocks differ from 256 at sample %zu", block, diff);
    }
}

/** Clock "Live": Slice mode records and plays on the host's beats. */
void TestClockFollowsHost()
{
    for (double bpm : { 120.0, 100.0 })
    {
        const double start = 0.25, beat = 60.0 / bpm;
        Processor p;
        std::vector<ProcStep> steps = {
            Set(0, SwitchParam(kModeA), 1.f),   // Slice
            Set(0, DeckControl(0, (int)Panel::Control::Mix), 1.f),
            Set(0, GlobalControl((int)Panel::Global::Crossfade), 0.f),
            RecordMidi(0.6), RecordMidi(2.7) };
        const auto r  = RunProcessor(p, kFs, 6.0, 480, steps, PlayingFrom(start, bpm), 2.7);
        const auto st = StateOf(p);

        // From the beat after the first press to the beat after the second:
        // four beats.
        const double expected = 4 * beat * kFs;
        const double onset    = Onset(r.l, kFs, 2.75);
        const double phase    = std::fmod(onset - start, beat);
        std::printf("  %3.0f BPM: module tempo %.2f, loop %.4f s (4 beats %.4f), playback at %.4f s, %.1f ms after a beat\n",
                    bpm, st.tempo, st.recSize / kFs, expected / kFs, onset, phase * 1000);
        CHECK(std::abs(st.tempo - bpm) < 0.5, "module tempo %.2f, host %.0f", st.tempo, bpm);
        CHECK(std::abs((double)st.recSize - expected) <= 192, "loop %zu, expected %.0f", st.recSize, expected);
        CHECK(phase < 0.015, "playback %.1f ms after the beat", phase * 1000);
    }
}

/** Live's transport stopped: the internal clock at Live's tempo. Clock
 *  "Internal": the Tempo control, whatever Live does. */
void TestClockModes()
{
    Processor stopped;
    RunProcessor(stopped, kFs, 0.3, 512, {}, [](double) {
        Processor::Transport t; t.valid = true; t.playing = false; t.bpm = 100; return t; });
    CHECK(std::abs(StateOf(stopped).tempo - 100) < 0.5, "stopped transport: tempo %.2f", StateOf(stopped).tempo);

    Processor internal;
    RunProcessor(internal, kFs, 0.5, 512,
                 { Set(0, SwitchParam(kClock), (float)kClockInternal), Set(0, GlobalControl((int)Panel::Global::Tempo), TempoNorm(135)) },
                 PlayingFrom(0.0, 100.0));
    CHECK(std::abs(StateOf(internal).tempo - 135) < 0.5, "internal clock: tempo %.2f", StateOf(internal).tempo);
}

/** Tapping Tap sets the tempo, and the Tempo parameter follows. */
void TestTapTempo()
{
    Processor p;
    std::vector<ProcStep> steps = { Set(0, SwitchParam(kClock), (float)kClockInternal) };
    for (int i = 0; i < 6; ++i)
        for (auto& s : Tap(0.5 + 0.4 * i, kTap, 0.05)) steps.push_back(s);
    RunProcessor(p, kFs, 3.5, 512, steps);
    const auto st = StateOf(p);
    const int  tempo = GlobalControl((int)Panel::Global::Tempo);
    std::printf("  tap: module tempo %.2f, Tempo parameter %.4f (150 BPM is %.4f)\n", st.tempo, p.getParameter(tempo), TempoNorm(150));
    CHECK(std::abs(st.tempo - 150) < 1.0, "tapped tempo %.2f", st.tempo);
    CHECK(p.takeChanged(tempo), "the Tempo parameter should report the change");
    CHECK(std::abs(p.getParameter(tempo) - TempoNorm(150)) < 0.01, "Tempo parameter %.4f", p.getParameter(tempo));
}

/** A MIDI CC sets the firmware's value, and the parameter follows. */
void TestMidiCc()
{
    Processor p;
    RunProcessor(p, kFs, 0.3, 512, { MidiAt(0.1, 0xB0, 22, 64) });   // CC 22: Size, channel 1
    const int size = DeckControl(0, (int)Panel::Control::Size);
    CHECK(std::abs(p.getParameter(size) - 64 / 127.f) < 1e-4, "A Size %.4f", p.getParameter(size));
    CHECK(p.takeChanged(size), "A Size should report the change");
    CHECK(!p.takeChanged(size), "reported once");
}

/** Alt + Seq arms the sequencer; holding Seq for 1.5 s clears it. */
void TestHoldToClear()
{
    auto armThenHold = [](double hold) {
        Processor p;
        std::vector<ProcStep> steps = Tap(0.5, kAlt, 0.1);
        for (auto& s : Tap(0.55, kSeqA, hold)) steps.push_back(s);
        RunProcessor(p, kFs, 0.6 + hold + 0.3, 512, steps);
        return StateOf(p).trackArmed;
    };
    CHECK(armThenHold(0.3), "a short press should leave the sequencer armed");
    CHECK(!armThenHold(1.7), "holding Seq for 1.5 s should clear it");
}

/** Alt held while Pitch moves: the speed snaps to a step. */
void TestPitchQuantise()
{
    Processor p;
    const int pitch = DeckControl(0, (int)Panel::Control::Pitch);
    auto steps = Tap(0.5, kAlt, 0.3);
    steps.push_back(Set(0.6, pitch, 0.6f));
    RunProcessor(p, kFs, 2.5, 512, steps);
    // CoreUI re-applies the snapped value once the display times out.
    std::printf("  quantised pitch: %.4f (step .5831)\n", p.getParameter(pitch));
    CHECK(std::abs(p.getParameter(pitch) - .5830516667f) < 1e-4, "Pitch %.4f", p.getParameter(pitch));
}

/** Slice mode, Tap held: Size fits the tempo to the loop. */
void TestSizeFitsTempo()
{
    Processor p;
    std::vector<ProcStep> steps = {
        Set(0, SwitchParam(kModeA), 1.f), Set(0, SwitchParam(kClock), (float)kClockInternal),
        RecordMidi(0.6), RecordMidi(2.7),
        Set(4.0, ButtonParam(kTap), 1.f),
        Set(4.3, DeckControl(0, (int)Panel::Control::Size), 0.f),   // one quarter... for the whole loop
        Set(4.5, ButtonParam(kTap), 0.f) };
    RunProcessor(p, kFs, 5.0, 512, steps);
    const auto st   = StateOf(p);
    const double fit = 2880000.0 / st.recSize;   // Deck::tempo_to_fit at the shortest size
    const int tempo  = GlobalControl((int)Panel::Global::Tempo);
    std::printf("  size fits tempo: loop %.3f s, module tempo %.2f (fit %.2f), Tempo parameter %.4f\n",
                st.recSize / kFs, st.tempo, fit, p.getParameter(tempo));
    CHECK(std::abs(st.tempo - fit) < 0.5, "tempo %.2f, expected %.2f", st.tempo, fit);
    CHECK(std::abs(p.getParameter(tempo) - TempoNorm(fit)) < 0.005, "Tempo parameter %.4f", p.getParameter(tempo));
}

/** Two processors in one process, taking turns block by block, each sound
 *  as they do alone. Without patches/instances/0002 they would share the
 *  firmware UI's tap state and clock edge: one instance's taps and MIDI
 *  clocks would reach the other. */
void TestInstances()
{
    auto firstDiff = [](const ProcRender& x, const ProcRender& y) {
        for (std::size_t i = 0; i < x.l.size(); ++i)
            if (x.l[i] != y.l[i] || x.r[i] != y.r[i]) return i;
        return x.l.size();
    };
    auto tapping = [](double start, double interval, int taps) {
        std::vector<ProcStep> s = { Set(0, SwitchParam(kClock), (float)kClockInternal) };
        for (int i = 0; i < taps; ++i)
            for (auto& x : Tap(start + interval * i, kTap, 0.05)) s.push_back(x);
        return Join({ s, RecordA(0.3), RecordA(1.0), { Set(0, SwitchParam(kModeA), 1.f) } });
    };
    auto following = [] {
        return Join({ RecordA(0.3), RecordA(1.9), { Set(0, SwitchParam(kModeA), 1.f) } });
    };

    struct Case { const char* name; std::vector<ProcStep> a, b; TransportAt ta, tb; double tempoA, tempoB; };
    const Case cases[] = {
        { "tap tempo", tapping(0.5, 0.4, 6), tapping(0.62, 0.6, 5), NoTransport, NoTransport, 150, 100 },
        { "host clocks", following(), following(), PlayingFrom(0, 120), PlayingFrom(0, 125), 120, 125 },
    };
    for (const auto& c : cases)
    {
        Processor soloA, soloB;
        const auto aloneA = RunProcessor(soloA, 44100.0, 4.0, 512, c.a, c.ta, 1.0);
        const auto aloneB = RunProcessor(soloB, 44100.0, 4.0, 333, c.b, c.tb, 1.0);

        Processor a, b;
        ProcPlayer pa(a, 44100.0, 4.0, c.a, c.ta, 1.0);
        ProcPlayer pb(b, 44100.0, 4.0, c.b, c.tb, 1.0);
        while (!pa.done() || !pb.done())
        {
            if (!pa.done()) pa.advance(512);
            if (!pb.done()) pb.advance(333);
        }
        const auto da = firstDiff(pa.out, aloneA), db = firstDiff(pb.out, aloneB);
        std::printf("  instances, %s: tempos %.2f and %.2f\n", c.name, StateOf(a).tempo, StateOf(b).tempo);
        CHECK(std::abs(StateOf(a).tempo - c.tempoA) < 1.0 && std::abs(StateOf(b).tempo - c.tempoB) < 1.0,
              "%s: tempos %.2f and %.2f", c.name, StateOf(a).tempo, StateOf(b).tempo);
        CHECK(da == aloneA.l.size(), "%s: first processor differs from its solo render at %zu", c.name, da);
        CHECK(db == aloneB.l.size(), "%s: second processor differs from its solo render at %zu", c.name, db);
    }
}

/** After the host jumps, the module locks to the new beat grid. */
void TestHostJump()
{
    // Plays from beat 0, then at 3.2 s (beat 6.4) jumps back to beat 0.
    auto transport = [](double t) {
        Processor::Transport tr;
        tr.valid = tr.playing = true;
        tr.bpm   = 120;
        tr.ppq   = t < 3.2 ? t * 2 : (t - 3.2) * 2;
        tr.barStartPpq = std::floor(tr.ppq / 4) * 4;
        return tr;
    };
    Processor p;
    std::vector<ProcStep> steps = {
        Set(0, SwitchParam(kModeA), 1.f), Set(0, DeckControl(0, (int)Panel::Control::Mix), 1.f),
        Set(0, GlobalControl((int)Panel::Global::Crossfade), 0.f),
        { 3.2, [](Processor&, Midi&) {} },   // the jump falls on a block
        RecordMidi(3.35), RecordMidi(5.45) };
    const auto r  = RunProcessor(p, kFs, 7.0, 480, steps, transport, 5.45);
    const auto st = StateOf(p);
    const double onset = Onset(r.l, kFs, 5.5);
    const double phase = std::fmod(onset - 3.2, 0.5);
    std::printf("  after a jump: loop %.4f s, playback at %.4f s, %.1f ms after a beat of the new grid\n",
                st.recSize / kFs, onset, phase * 1000);
    CHECK(std::abs((double)st.recSize - 2.0 * kFs) <= 192, "loop %zu", st.recSize);
    CHECK(phase < 0.015, "playback %.1f ms after the beat", phase * 1000);
}

/** Grit while held; Alt + Grit locks it on. */
void TestFxHoldAndLock()
{
    auto gritOn = [](std::vector<ProcStep> steps, double until) {
        Processor p;
        RunProcessor(p, kFs, until, 256, steps);
        return p.module().control([](Panel&, Core& c) { return c.deck(Deck::A).fx().is_grit_on(); });
    };
    CHECK(gritOn(Tap(0.2, kGritA, 0.5), 0.5), "Grit should be on while held");
    CHECK(!gritOn(Tap(0.2, kGritA, 0.2), 0.6), "and off once released");
    auto lock = Tap(0.2, kAlt, 0.2);
    for (auto& s : Tap(0.25, kGritA, 0.05)) lock.push_back(s);
    CHECK(gritOn(lock, 0.8), "Alt + Grit should lock it on");
}

/** Clicks shorter than a host block: pressed and released between two
 *  process() calls, they still reach the firmware. */
void TestQuickClicks()
{
    Processor p;
    std::vector<float> l(2048), r(2048);
    auto block = [&] {
        std::fill(l.begin(), l.end(), 0.f);
        std::fill(r.begin(), r.end(), 0.f);
        p.process(l.data(), r.data(), l.data(), r.data(), 2048, {});
    };
    auto click = [&](Button b) { p.setParameter(ButtonParam(b), 1.f); p.setParameter(ButtonParam(b), 0.f); };

    block();
    CHECK(p.deckStatus(0).state == Processor::DeckState::Empty, "deck A starts empty");
    p.setParameter(ButtonParam(kAlt), 1.f);
    click(kPlayA);
    p.setParameter(ButtonParam(kAlt), 0.f);
    for (int i = 0; i < 3; ++i) block();
    CHECK(StateOf(p).armed, "Alt + Play, both quicker than a block, should arm deck A");
    CHECK(p.deckStatus(0).state == Processor::DeckState::Armed, "deck A status %d", (int)p.deckStatus(0).state);

    // Digital silence keeps it waiting, as the module's noisy input does;
    // sound starts the recording.
    for (int i = 0; i < 25; ++i) block();   // 1 s
    CHECK(p.deckStatus(0).state == Processor::DeckState::Armed, "still armed after 1 s of silence (%d)", (int)p.deckStatus(0).state);
    for (int k = 0; k < 2048; ++k) l[k] = r[k] = 0.3f * std::sin((float)k * 0.06f);
    p.process(l.data(), r.data(), l.data(), r.data(), 2048, {});
    p.process(l.data(), r.data(), l.data(), r.data(), 2048, {});
    CHECK(p.deckStatus(0).state == Processor::DeckState::Recording, "sound starts the recording (%d)", (int)p.deckStatus(0).state);

    bool lit = false;
    for (int i = 0; i < Processor::kMaxLeds; ++i) lit |= p.led(i) != 0;
    CHECK(lit, "the LEDs should be published");

    // Tap tempo from quick clicks, 0.4 s apart: 150 BPM.
    Processor t;
    t.setParameter(SwitchParam(kClock), (float)kClockInternal);
    auto tblock = [&] { t.process(l.data(), r.data(), l.data(), r.data(), 480, {}); };
    for (int i = 0; i < 10; ++i) tblock();
    for (int tap = 0; tap < 6; ++tap)
    {
        t.setParameter(ButtonParam(kTap), 1.f);
        t.setParameter(ButtonParam(kTap), 0.f);
        for (int i = 0; i < 40; ++i) tblock();   // 0.4 s
    }
    const auto tempo = StateOf(t).tempo;
    std::printf("  quick clicks: deck A armed, tapped tempo %.2f\n", tempo);
    CHECK(std::abs(tempo - 150) < 1.0, "quick taps should set the tempo (%.2f)", tempo);
}

} // namespace

int main()
{
    std::printf("processor_test\n");
    TestLoopAtRates();
    TestBlockSizes();
    TestClockFollowsHost();
    TestClockModes();
    TestTapTempo();
    TestMidiCc();
    TestHoldToClear();
    TestPitchQuantise();
    TestSizeFitsTempo();
    TestInstances();
    TestHostJump();
    TestFxHoldAndLock();
    TestQuickClicks();
    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
