/**
 * @file stress_test.cpp
 * @brief Everything at random, as a validator or careless automation would
 *        do it: every parameter (pads and switches included) set to a
 *        random value before each block, random MIDI, a host transport that
 *        jumps about. The module must keep up and its output stay finite.
 *
 * This found the frozen LED ring (patches/fixes/0001): pluginval's
 * "Parameter thread safety" test hung on it. CTest times the test out if
 * anything hangs again.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "Check.h"
#include "ScotykachParams.h"
#include "ScotykachProcessor.h"

using namespace scotykach;
namespace prm = scotykach::params;

/** stress_test [first last]: seeds to run (default 1 to 8). */
int main(int argc, char** argv)
{
    const int first = argc > 2 ? std::atoi(argv[1]) : 1;
    const int last  = argc > 2 ? std::atoi(argv[2]) : 8;
    std::printf("stress_test, seeds %d to %d\n", first, last);
    for (int seed = first; seed <= last; ++seed)
    {
        std::mt19937 rng((uint32_t)seed);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        const double rate = seed % 2 ? 44100.0 : 96000.0;

        Processor p;
        p.prepare(rate);
        std::vector<float> l(512), r(512);
        Processor::Transport t;
        t.valid = true;
        bool finite = true;

        for (int b = 0; b < 500; ++b)
        {
            for (int k = 0; k < prm::kCount; ++k)
            {
                const auto& i = prm::Get(k);
                float v = u(rng);
                if (i.kind == prm::Kind::Choice) v = (float)(int)(v * i.numChoices);
                if (i.kind == prm::Kind::Cv)     v = v * 2 - 1;
                p.setParameter(k, v);
            }
            std::vector<Processor::MidiMessage> midi;
            for (int m = 0; m < 3; ++m)
            {
                const uint8_t kinds[] = { 0x90, 0x80, 0xB0, 0xF8, 0xFA, 0xFC };
                uint8_t status = kinds[rng() % 6];
                if (status < 0xF0) status = (uint8_t)(status | (rng() % 3));   // channels 1-3
                midi.push_back({ (int)(rng() % 512), { status, (uint8_t)(rng() % 128), (uint8_t)(rng() % 128) } });
            }
            std::sort(midi.begin(), midi.end(), [](auto& a, auto& c) { return a.offset < c.offset; });

            // Plays, stops, jumps and changes tempo now and then.
            if (rng() % 20 == 0) t.playing = !t.playing;
            if (rng() % 30 == 0) t.ppq = u(rng) * 64;
            if (rng() % 40 == 0) t.bpm = 40 + u(rng) * 200;
            t.barStartPpq = std::floor(t.ppq / 4) * 4;

            for (int i = 0; i < 512; ++i) l[i] = r[i] = u(rng) * 2 - 1;
            p.process(l.data(), r.data(), l.data(), r.data(), 512, t, midi.data(), (int)midi.size());
            for (int i = 0; i < 512; ++i) finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            if (t.playing) t.ppq += 512 * t.bpm / (60.0 * rate);
        }
        CHECK(finite, "seed %d: non-finite output", seed);
    }
    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
