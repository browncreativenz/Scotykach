/**
 * @file engine_test.cpp
 * @brief The firmware engine off the hardware: Reel, Slice and Drift record
 *        and loop; output doesn't depend on the host block size; every mode,
 *        route and control in the scenarios gives finite output.
 */

#include <cmath>
#include <cstdio>

#include "Check.h"
#include "Scenarios.h"

using namespace scotykach;
using namespace scotykach::test;
using spotykach::Deck;

namespace {

constexpr double kFs = Engine::kSampleRate;

std::size_t At(double seconds) { return (std::size_t)(seconds * kFs); }

double Rms(const std::vector<float>& x, double from, double to)
{
    double sum = 0.0;
    for (std::size_t i = At(from); i < At(to); ++i) sum += (double)x[i] * x[i];
    return std::sqrt(sum / (double)(At(to) - At(from)));
}

/** Normalised correlation of x over [from, from + len) with x shifted by lag. */
double Correlation(const std::vector<float>& x, double from, double len, std::size_t lag)
{
    double xy = 0, xx = 0, yy = 0;
    for (std::size_t i = At(from); i < At(from + len); ++i)
    {
        const double a = x[i], b = x[i + lag];
        xy += a * b; xx += a * a; yy += b * b;
    }
    return xy / std::sqrt(xx * yy + 1e-30);
}

struct DeckState
{
    std::size_t recSize;
    bool        playing;
};

DeckState StateOf(Engine& e)
{
    return e.control([](spotykach::Core& c) {
        auto& d = c.deck(Deck::A);
        return DeckState { d.buffer().rec_size(), d.is_playing() };
    });
}

void TestReel()
{
    Engine e;
    const auto r = Run(Loop("reel"), 512, &e);
    CHECK(r.error.empty(), "%s", r.error.c_str());
    const auto st = StateOf(e);

    // Recording starts at the first onset after the press, so the loop is at
    // most the two seconds between the presses.
    std::printf("  reel: loop %.3f s, rms %.3f\n", st.recSize / kFs, Rms(r.l, 3.0, 6.0));
    CHECK(st.recSize > At(1.8) && st.recSize <= At(2.0) + Engine::kBlock, "loop %zu samples", st.recSize);
    CHECK(st.playing, "deck A should be playing");
    CHECK(Rms(r.l, 3.0, 6.0) > 0.05, "the loop should be playing with the input muted");

    // The output repeats with the loop's length.
    const double c = Correlation(r.l, 3.0, 0.5, st.recSize);
    std::printf("  reel: correlation one loop apart %.3f\n", c);
    CHECK(c > 0.9, "correlation %.3f", c);
}

void TestSlice()
{
    Engine e;
    const auto r = Run(Loop("slice"), 512, &e);
    CHECK(r.error.empty(), "%s", r.error.c_str());
    const auto st = StateOf(e);

    // Slice mode records on the clock: from a beat to a beat, so the two
    // seconds between the presses become exactly four beats at 120 BPM.
    std::printf("  slice: loop %.4f s\n", st.recSize / kFs);
    CHECK(std::abs((double)st.recSize - At(2.0)) <= 2.0, "loop %zu samples, expected 96000", st.recSize);
    CHECK(st.playing, "deck A should be playing");

    // Playback waits for the next beat after the loop closes (3 s), and is
    // heard within a few milliseconds of it: the clock moves in 2 ms steps
    // and each slice fades in.
    std::size_t onset = At(kRecEnd);
    while (onset < r.l.size() && std::abs(r.l[onset]) < 1e-4f) ++onset;
    std::printf("  slice: playback starts at %.4f s\n", onset / kFs);
    CHECK(onset >= At(3.0) && onset < At(3.015), "playback at %.4f s", onset / kFs);
    CHECK(Rms(r.l, 3.5, 6.0) > 0.05, "the loop should be playing with the input muted");
}

void TestDrift()
{
    const auto drift = Run(Loop("drift"), 512);
    const auto reel  = Run(Loop("reel"), 512);
    CHECK(drift.error.empty(), "%s", drift.error.c_str());
    std::printf("  drift: rms %.3f\n", Rms(drift.l, 3.0, 6.0));
    CHECK(Rms(drift.l, 3.0, 6.0) > 0.05, "Drift should play grains of the loop");

    double diff = 0;
    for (std::size_t i = At(3.0); i < At(6.0); ++i) diff += std::abs(drift.l[i] - reel.l[i]);
    CHECK(diff > 1.0, "Drift should not sound like Reel");
}

/** The FIFO runs the firmware on every 96th sample whatever the host sends. */
void TestBlockSizes()
{
    for (const auto& s : { Loop("slice"), Performance("drift", "gen") })
    {
        const auto ref = Run(s, 64);
        for (std::size_t block : { 1, 7, 96, 500, 4096 })
        {
            const auto r = Run(s, block);
            std::size_t firstDiff = r.l.size();
            for (std::size_t i = 0; i < r.l.size(); ++i)
                if (r.l[i] != ref.l[i] || r.r[i] != ref.r[i]) { firstDiff = i; break; }
            CHECK(firstDiff == r.l.size(), "%s, %zu-sample blocks: differs from 64-sample blocks at %zu",
                  s.name.c_str(), block, firstDiff);
        }
    }
}

/** Every scenario: the script runs and the output stays finite and sane. */
void TestAllScenarios()
{
    for (const auto& s : AllScenarios())
    {
        const auto r = Run(s, 512);
        CHECK(r.error.empty(), "%s: %s", s.name.c_str(), r.error.c_str());
        float peak = 0.f;
        bool  finite = true;
        for (std::size_t i = 0; i < r.l.size(); ++i)
        {
            finite = finite && std::isfinite(r.l[i]) && std::isfinite(r.r[i]);
            peak   = std::max({ peak, std::abs(r.l[i]), std::abs(r.r[i]) });
        }
        std::printf("  %-28s peak %.3f, rms after the loop closes %.3f\n",
                    s.name.c_str(), peak, Rms(r.l, 3.0, s.seconds - 0.5));
        CHECK(finite, "%s: non-finite output", s.name.c_str());
        CHECK(peak < 2.0f, "%s: peak %.3f", s.name.c_str(), peak);
        CHECK(Rms(r.l, 3.0, 4.0) + Rms(r.r, 3.0, 4.0) > 0.02, "%s: silent after the loop closed", s.name.c_str());
    }
}

} // namespace

int main()
{
    std::printf("engine_test\n");
    TestReel();
    TestSlice();
    TestDrift();
    TestBlockSizes();
    TestAllScenarios();
    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
