/**
 * @file Scenarios.h
 * @brief Scripted performances shared by the tests.
 *
 * Each scenario records the test signal into deck A between 0.5 s and 2.5 s
 * (two presses of Record), mutes the input when the loop closes, and lets
 * the loop play on. Deck A is fully wet and the crossfader is on A, so the
 * output is the deck alone.
 */

#pragma once

#include <string>
#include <vector>

#include "Script.h"
#include "TestSignal.h"

namespace scotykach::test {

constexpr double kRecStart = 0.5;   // first Record press
constexpr double kRecEnd   = 2.5;   // second press: the loop closes, input muted

struct Scenario
{
    std::string               name;
    double                    seconds;
    std::vector<script::Step> steps;
};

inline Scenario Loop(const std::string& mode, double seconds = 6.0)
{
    return { mode, seconds,
             { { 0.0, "mode:A=" + mode }, { 0.0, "sos:A=1" }, { 0.0, "crossfade=0" },
               { kRecStart, "rec:A" }, { kRecEnd, "rec:A" } } };
}

/** The loop, then knobs, reverse and FX moving while it plays. */
inline Scenario Performance(const std::string& mode, const std::string& route = "stereo")
{
    auto s  = Loop(mode, 9.0);
    s.name  = mode + "-" + route + "-performance";
    s.steps.push_back({ 0.0, "route=" + route });
    for (const auto& step : std::vector<script::Step> {
             { 3.0, "pitch:A=0.8" }, { 3.5, "pos:A=0.3" },  { 4.0, "size:A=0.4" },
             { 4.5, "rev:A" },       { 5.0, "env:A=0.7" },  { 5.0, "grit:A=on" },
             { 5.5, "gritamt:A=0.6" },{ 6.0, "flux:A=on" }, { 6.5, "fluxamt:A=0.4" },
             { 7.0, "rev:A" },       { 7.0, "pitch:A=0.2" },{ 7.5, "win:A=0.9" },
             { 8.0, "grit:A=off" },  { 8.0, "flux:A=off" }, { 8.5, "play:A" } })
        s.steps.push_back(step);
    return s;
}

/** All scenarios the cross-checks render. */
inline std::vector<Scenario> AllScenarios()
{
    std::vector<Scenario> all;
    for (const char* mode : { "reel", "slice", "drift" })
    {
        all.push_back(Loop(mode));
        for (const char* route : { "stereo", "mono", "gen" })
            all.push_back(Performance(mode, route));
    }
    return all;
}

struct Render
{
    std::vector<float> l, r;
    std::string        error;
};

/** The scenario's input: the test signal, muted from kRecEnd. */
inline Render Input(const Scenario& s)
{
    const std::size_t n = (std::size_t)(s.seconds * Engine::kSampleRate);
    Render     in;
    TestSignal sig((float)Engine::kSampleRate);
    in.l.resize(n);
    in.r.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        sig.Next(in.l[i], in.r[i]);
        if (i >= (std::size_t)(kRecEnd * Engine::kSampleRate)) in.l[i] = in.r[i] = 0.f;
    }
    return in;
}

/** The scenario through a fresh engine (or the one given) in host blocks of
 *  `block` samples. */
inline Render Run(const Scenario& s, std::size_t block, Engine* engine = nullptr)
{
    const auto              in = Input(s);
    std::unique_ptr<Engine> own;
    if (engine == nullptr) { own = std::make_unique<Engine>(); engine = own.get(); }

    Render        out;
    script::Panel panel;
    out.l.resize(in.l.size());
    out.r.resize(in.l.size());
    out.error = script::Render(*engine, panel, s.steps, in.l.data(), in.r.data(),
                               out.l.data(), out.r.data(), in.l.size(), block);
    return out;
}

} // namespace scotykach::test
