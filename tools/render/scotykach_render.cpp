/**
 * @file scotykach_render.cpp
 * @brief Renders audio through the Spotykach engine, with scripted actions.
 *
 *   scotykach_render out.wav                     the built-in demo
 *   scotykach_render --in loop.wav --at 0.5 rec:A --at 2.5 rec:A out.wav
 *
 * Options:
 *   --in FILE        input WAV, 48 kHz (default: a drums-and-pad test signal)
 *   --seconds N      length of the render (default: the input's, or 8 s)
 *   --at T ACTION    apply ACTION at T seconds; repeatable (see tools/Script.h)
 *   --mute-from T    silence the input from T seconds on
 *   --block N        host block size (default 512)
 *
 * Without --at, renders a demo: deck A in Reel mode records two seconds of
 * the input and loops them, with the input muted after the loop closes.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Script.h"
#include "TestSignal.h"
#include "Wav.h"

using namespace scotykach;

namespace {

using script::Step;

int Usage()
{
    std::fprintf(stderr,
        "usage: scotykach_render [--in FILE] [--seconds N] [--at T ACTION]... "
        "[--mute-from T] [--block N] out.wav\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::string       inPath, outPath;
    double            seconds  = -1.0;
    double            muteFrom = -1.0;
    int               block    = 512;
    std::vector<Step> steps;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--in")             { auto v = next(); if (!v) return Usage(); inPath = v; }
        else if (a == "--seconds")   { auto v = next(); if (!v) return Usage(); seconds = std::atof(v); }
        else if (a == "--mute-from") { auto v = next(); if (!v) return Usage(); muteFrom = std::atof(v); }
        else if (a == "--block")     { auto v = next(); if (!v) return Usage(); block = std::max(1, std::atoi(v)); }
        else if (a == "--at")
        {
            auto t = next(); auto act = next();
            if (!t || !act) return Usage();
            steps.push_back({ std::atof(t), act });
        }
        else if (!a.empty() && a[0] == '-') return Usage();
        else if (outPath.empty()) outPath = a;
        else return Usage();
    }
    if (outPath.empty()) return Usage();

    StereoAudio in;
    if (!inPath.empty())
    {
        std::string error;
        if (!ReadWav(inPath, in, error)) { std::fprintf(stderr, "%s: %s\n", inPath.c_str(), error.c_str()); return 1; }
        if (in.sampleRate != 48000)
        {
            std::fprintf(stderr, "%s: %u Hz; the engine runs at 48 kHz (other rates come with the plugin)\n",
                         inPath.c_str(), in.sampleRate);
            return 1;
        }
        if (seconds < 0) seconds = (double)in.l.size() / 48000.0;
    }
    else
    {
        if (seconds < 0) seconds = 8.0;
        TestSignal sig(48000.f);
        in.l.resize((size_t)(seconds * 48000.0));
        in.r.resize(in.l.size());
        for (size_t i = 0; i < in.l.size(); ++i) sig.Next(in.l[i], in.r[i]);
    }

    if (steps.empty())
    {
        steps = { { 0.0, "mode:A=reel" }, { 0.0, "sos:A=1" }, { 0.0, "crossfade=0" },
                  { 0.5, "rec:A" }, { 2.5, "rec:A" } };
        if (muteFrom < 0) muteFrom = 2.5;
    }
    const size_t total = (size_t)(seconds * 48000.0);
    in.l.resize(std::max(in.l.size(), total));
    in.r.resize(in.l.size());
    if (muteFrom >= 0)
        for (size_t i = (size_t)(muteFrom * 48000.0); i < in.l.size(); ++i) in.l[i] = in.r[i] = 0.f;

    Engine            engine;
    script::Panel     panel;
    StereoAudio       out;
    out.l.resize(total);
    out.r.resize(total);

    const auto error = script::Render(engine, panel, steps, in.l.data(), in.r.data(),
                                      out.l.data(), out.r.data(), total, (size_t)block);
    if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }

    if (!WriteWav(outPath, out)) { std::fprintf(stderr, "%s: could not write\n", outPath.c_str()); return 1; }
    std::printf("%s: %.2f s at 48 kHz\n", outPath.c_str(), seconds);
    return 0;
}
