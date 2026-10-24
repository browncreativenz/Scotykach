/**
 * @file module_test.cpp
 * @brief The whole firmware, engine and UI: driven through the panel, it
 *        gives exactly the samples the engine alone gives for the same
 *        performance; its LEDs and gate outputs respond as on the module.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Check.h"
#include "Scenarios.h"
#include "ScotykachModule.h"

using namespace scotykach;
using namespace scotykach::test;
using namespace spotykach;
using P = Hardware::Pad;

namespace {

/** A script action, done the panel's way: pads, switches and parameters. */
void ApplyToPanel(Panel& panel, const std::string& action)
{
    const auto eq    = action.find('=');
    const auto lhs   = action.substr(0, eq);
    const auto value = eq == std::string::npos ? std::string() : action.substr(eq + 1);
    const auto colon = lhs.find(':');
    const auto name  = lhs.substr(0, colon);
    const auto ref   = colon != std::string::npos && lhs.substr(colon + 1) == "B" ? Deck::B : Deck::A;
    const float v    = std::strtof(value.c_str(), nullptr);
    const bool  a    = ref == Deck::A;

    if (name == "rec")
    {
        // Alt held, tap Play: the module's record gesture.
        panel.pad(P::Alt, true);
        panel.pad(a ? P::PlayA : P::PlayB, true);
        panel.pad(a ? P::PlayA : P::PlayB, false);
        panel.pad(P::Alt, false);
    }
    else if (name == "mode")  panel.setMode(ref, value == "reel" ? Mode::Reel : value == "slice" ? Mode::Slice : Mode::Drift);
    else if (name == "route") panel.setRoute(value == "stereo" ? Panel::Route::Stereo
                                           : value == "mono"   ? Panel::Route::DoubleMono
                                                               : Panel::Route::GenerativeStereo);
    else if (name == "grit")  panel.pad(a ? P::GritA : P::GritB, value == "on");
    else if (name == "flux")  panel.pad(a ? P::FluxA : P::FluxB, value == "on");
    else if (name == "crossfade") panel.set(Panel::Global::Crossfade, v);
    else
    {
        static const struct { const char* name; Panel::Control c; } map[] = {
            { "sos", Panel::Control::Mix },      { "pitch", Panel::Control::Pitch },
            { "pos", Panel::Control::Position }, { "size", Panel::Control::Size },
            { "env", Panel::Control::Envelope }, { "win", Panel::Control::Window },
            { "envsize", Panel::Control::EnvelopeSize },
            { "gritamt", Panel::Control::GritIntensity }, { "fluxamt", Panel::Control::FluxTime },
        };
        for (const auto& m : map)
            if (name == m.name) { panel.set(ref, m.c, v); return; }
        std::printf("  (no panel action for %s)\n", action.c_str());
        test::FailureCount()++;
    }
}

/** The loop, then knobs and FX moving while it plays (no Play or Rev: the
 *  module's pads mean more than the engine script's toggles). */
Scenario Knobs(const std::string& mode, const std::string& route)
{
    auto s  = Loop(mode, 9.0);
    s.name  = mode + "-" + route + "-knobs";
    // First: when both switches move at once, CoreUI::_process_switches
    // applies the route before the mode.
    s.steps.insert(s.steps.begin(), { 0.0, "route=" + route });
    for (const auto& step : std::vector<script::Step> {
             { 3.0, "pitch:A=0.8" }, { 3.5, "pos:A=0.3" },   { 4.0, "size:A=0.4" },
             { 5.0, "env:A=0.7" },   { 5.0, "grit:A=on" },   { 5.5, "gritamt:A=0.6" },
             { 6.0, "flux:A=on" },   { 6.5, "fluxamt:A=0.4" }, { 7.0, "pitch:A=0.2" },
             { 7.5, "win:A=0.9" },   { 8.0, "grit:A=off" },  { 8.0, "flux:A=off" } })
        s.steps.push_back(step);
    return s;
}

Render RunModule(const Scenario& s, std::size_t block, Module& m)
{
    const auto in = Input(s);
    Render out;
    out.l.resize(in.l.size());
    out.r.resize(in.l.size());

    auto steps = s.steps;
    std::stable_sort(steps.begin(), steps.end(),
                     [](const script::Step& x, const script::Step& y) { return x.time < y.time; });
    auto at = [](double t) { return (std::size_t)(t * Module::kSampleRate); };
    std::size_t pos = 0, next = 0;
    while (pos < in.l.size())
    {
        for (; next < steps.size() && at(steps[next].time) <= pos; ++next)
            m.control([&](Panel& p, Core&) { ApplyToPanel(p, steps[next].action); });
        std::size_t len = std::min(block, in.l.size() - pos);
        if (next < steps.size()) len = std::min(len, at(steps[next].time) - pos);
        m.process(&in.l[pos], &in.r[pos], &out.l[pos], &out.r[pos], len);
        pos += len;
    }
    return out;
}

void TestSameAsEngine()
{
    std::vector<Scenario> all;
    for (const char* mode : { "reel", "slice", "drift" })
    {
        all.push_back(Loop(mode));
        for (const char* route : { "stereo", "mono", "gen" }) all.push_back(Knobs(mode, route));
    }
    for (const auto& s : all)
    {
        Module m;
        const auto mod = RunModule(s, 512, m);
        const auto eng = Run(s, 512);
        std::size_t diff = mod.l.size();
        for (std::size_t i = 0; i < mod.l.size(); ++i)
            if (mod.l[i] != eng.l[i] || mod.r[i] != eng.r[i]) { diff = i; break; }
        CHECK(diff == mod.l.size(), "%s: the module differs from the engine at %.4f s",
              s.name.c_str(), diff / Module::kSampleRate);
    }
    std::printf("  module = engine for %zu scenarios\n", all.size());
}

/** The Play LED lights while deck A plays forward; Rev stays dark. */
void TestPlayLed()
{
    Module m;
    const auto atBoot = m.hardware().leds.showCount();   // the start-up animation, run at once
    RunModule(Loop("reel", 3.5), 512, m);
    const auto& px = m.hardware().leds.shown();
    std::printf("  LEDs shown %llu times after boot; Play A %.2f, Rev A %.2f\n",
                (unsigned long long)(m.hardware().leds.showCount() - atBoot),
                px[Hardware::LED_PLAY_A].brightness, px[Hardware::LED_REV_A].brightness);
    CHECK(px[Hardware::LED_PLAY_A].brightness > 0.f, "Play A should be lit");
    CHECK(px[Hardware::LED_REV_A].brightness == 0.f, "Rev A should be dark");
    // 125 Hz, as on the module: every fourth 2 ms timer call.
    const double seconds = m.blocks() * Module::kBlock / Module::kSampleRate;
    const auto shows   = m.hardware().leds.showCount() - atBoot;
    CHECK(std::abs(shows - 125.0 * seconds) < 2.0, "LED refreshes %llu in %.2f s", (unsigned long long)shows, seconds);
}

/** A note on channel 1 retriggers deck A, and gate out A pulses (7 ms). */
void TestNoteGate()
{
    Module m;
    auto s = Loop("slice", 3.2);
    RunModule(s, 512, m);   // the loop plays from 3.0 s

    // Wait for the gate to settle low, then send the note.
    float zero[96] = {}, out[96];
    int lowBlocks = 0;
    for (int i = 0; i < 500 && lowBlocks < 8; ++i)
    {
        m.process(zero, zero, out, out, 96);
        lowBlocks = m.hardware().gateOut[0] ? 0 : lowBlocks + 1;
    }
    m.control([](Panel& p, Core&) { p.midiIn(0x90, 67, 100); });
    int high = 0;
    for (int i = 0; i < 6; ++i)
    {
        m.process(zero, zero, out, out, 96);
        high += m.hardware().gateOut[0] ? 1 : 0;
    }
    std::printf("  note on: gate A high for %d of 6 blocks\n", high);
    CHECK(high >= 3 && high <= 5, "gate A high for %d blocks, expected about 7 ms", high);
}

/** A loop read out of one module and written into another comes back the
 *  same, and plays. */
void TestLoopRoundTrip()
{
    Module from;
    RunModule(Loop("reel"), 512, from);
    std::vector<float> loop(2 * Panel::maxLoopFrames());
    const auto frames = from.control([&](Panel& p, Core&) { return p.readLoop(Deck::A, loop.data(), Panel::maxLoopFrames()); });
    CHECK(frames > 90000, "a loop of %zu frames", frames);

    for (const auto mode : { Mode::Reel, Mode::Slice })
    {
        Module to;
        to.control([&](Panel& p, Core&) {
            p.setMode(Deck::A, mode);
            p.set(Deck::A, Panel::Control::Mix, 1.f);
            p.set(Panel::Global::Crossfade, 0.f);
        });
        float zero[96] = {}, l[96], r[96];
        to.process(zero, zero, l, r, 96);   // the mode switch lands
        to.control([&](Panel& p, Core&) { p.writeLoop(Deck::A, loop.data(), frames); });

        std::vector<float> back(2 * frames);
        const auto got = to.control([&](Panel& p, Core&) { return p.readLoop(Deck::A, back.data(), frames); });
        CHECK(got == frames && back == std::vector<float>(loop.begin(), loop.begin() + 2 * frames),
              "the loop should read back as written");

        to.control([](Panel& p, Core&) { p.pad(P::PlayA, true); p.pad(P::PlayA, false); });
        double energy = 0;
        for (int b = 0; b < 1500; ++b)   // 3 s
        {
            to.process(zero, zero, l, r, 96);
            for (int i = 0; i < 96; ++i) energy += l[i] * l[i];
        }
        CHECK(energy > 100.0, "%s: the loaded loop should play (energy %.1f)", mode == Mode::Reel ? "Reel" : "Slice", energy);
    }

    Module empty;
    empty.control([&](Panel& p, Core&) { p.writeLoop(Deck::A, loop.data(), frames); p.writeLoop(Deck::A, nullptr, 0); });
    CHECK(empty.control([](Panel& p, Core&) { return p.loopFrames(Deck::A); }) == 0, "0 frames empties the deck");
}

} // namespace

int main()
{
    std::printf("module_test\n");
    TestSameAsEngine();
    TestPlayLed();
    TestNoteGate();
    TestLoopRoundTrip();
    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
