/**
 * @file scotykach_render.cpp
 * @brief Renders audio through the whole module (firmware engine and UI),
 *        as the plugin runs it, with scripted actions.
 *
 *   scotykach_render out.wav                         the built-in demo
 *   scotykach_render --in loop.wav --at 0.5 rec:A --at 2.5 rec:A out.wav
 *   scotykach_render --list                          every parameter
 *
 * Options:
 *   --in FILE        input WAV at any rate (default: a drums-and-pad test signal)
 *   --rate HZ        rate of the test signal and output (default 48000)
 *   --seconds N      length of the render (default: the input's, or 8 s)
 *   --at T ACTION    do ACTION at T seconds; repeatable
 *   --mute-from T    silence the input from T seconds on
 *   --bpm B          a host transport at B BPM in 4/4 (default: none)
 *   --play-from T    when that transport starts (default 0)
 *   --block N        host block size (default 512)
 *
 * Actions:
 *   ID=VALUE         set a parameter (see --list): a_pitch=0.8, a_mode=slice,
 *                    clock=internal; switch positions by name or number
 *   press:ID         hold a button: press:alt, press:a_grit
 *   release:ID       let it go
 *   tap:ID           press, and release 20 ms later
 *   rec:A, rec:B     record the module's way: Alt held, tap Play
 *   midi:B0,0E,7F    a MIDI message, in hex (channel 1 is deck A, 2 deck B)
 *
 * Without --at, renders a demo: deck A in Reel mode records two seconds of
 * the input and loops them, with the input muted after the loop closes.
 */

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "HostRig.h"
#include "ScotykachParams.h"
#include "Wav.h"

using namespace scotykach;
using namespace scotykach::rig;
namespace prm = scotykach::params;

namespace {

int Usage()
{
    std::fprintf(stderr,
        "usage: scotykach_render [--in FILE] [--rate HZ] [--seconds N] [--at T ACTION]...\n"
        "                        [--mute-from T] [--bpm B [--play-from T]] [--block N] out.wav\n"
        "       scotykach_render --list\n");
    return 2;
}

std::string Lower(std::string s)
{
    for (auto& c : s) c = c == ' ' ? '_' : (char)std::tolower((unsigned char)c);
    return s;
}

int FindParam(const std::string& id)
{
    for (int p = 0; p < prm::kCount; ++p)
        if (id == prm::Get(p).id) return p;
    return -1;
}

void List()
{
    for (int p = 0; p < prm::kCount; ++p)
    {
        const auto& i = prm::Get(p);
        std::printf("%-16s %-20s ", i.id, i.name);
        switch (i.kind)
        {
            case prm::Kind::Unit:   std::printf("0..1, default %.4g\n", i.defaultValue); break;
            case prm::Kind::Cv:     std::printf("-1..1 (CV), default 0\n"); break;
            case prm::Kind::Button: std::printf("button\n"); break;
            case prm::Kind::Choice:
                for (int c = 0; c < i.numChoices; ++c)
                    std::printf("%s%s%s", c ? " | " : "", Lower(i.choices[c]).c_str(), c == (int)i.defaultValue ? " (default)" : "");
                std::printf("\n");
                break;
        }
    }
}

/** Parses one action into steps, or returns an error. */
std::string Parse(double t, const std::string& action, std::vector<ProcStep>& steps)
{
    if (action == "rec:A" || action == "rec:a" || action == "rec:B" || action == "rec:b")
    {
        const auto play = (action.back() == 'A' || action.back() == 'a') ? prm::kPlayA : prm::kPlayB;
        for (auto& s : Tap(t, prm::kAlt, 0.06)) steps.push_back(s);
        for (auto& s : Tap(t + 0.02, play)) steps.push_back(s);
        return {};
    }
    if (action.rfind("midi:", 0) == 0)
    {
        unsigned b[3] = { 0, 0, 0 };
        const int got = std::sscanf(action.c_str() + 5, "%x,%x,%x", &b[0], &b[1], &b[2]);
        if (got < 1 || b[0] < 0x80 || b[0] > 0xFF) return "'" + action + "': give the bytes in hex, as midi:90,3C,64";
        steps.push_back(MidiAt(t, (uint8_t)b[0], (uint8_t)b[1], (uint8_t)b[2]));
        return {};
    }
    for (const char* verb : { "press:", "release:", "tap:" })
    {
        if (action.rfind(verb, 0) != 0) continue;
        const int p = FindParam(action.substr(std::string(verb).size()));
        if (p < 0 || prm::Get(p).kind != prm::Kind::Button) return "'" + action + "': not a button (see --list)";
        const std::string v = verb;
        if (v == "tap:") { steps.push_back(Set(t, p, 1.f)); steps.push_back(Set(t + 0.02, p, 0.f)); }
        else steps.push_back(Set(t, p, v == "press:" ? 1.f : 0.f));
        return {};
    }

    const auto eq = action.find('=');
    if (eq == std::string::npos) return "unknown action '" + action + "'";
    const int p = FindParam(action.substr(0, eq));
    if (p < 0) return "'" + action.substr(0, eq) + "' is not a parameter (see --list)";
    const auto& info  = prm::Get(p);
    const auto  value = action.substr(eq + 1);
    char*       end   = nullptr;
    float       v     = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0')
    {
        v = -1.f;
        for (int c = 0; info.kind == prm::Kind::Choice && c < info.numChoices; ++c)
            if (Lower(value) == Lower(info.choices[c])) v = (float)c;
        if (v < 0) return "'" + action + "': not a value of " + info.id + " (see --list)";
    }
    steps.push_back(Set(t, p, v));
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    std::string inPath, outPath;
    double seconds = -1.0, muteFrom = -1.0, rate = 48000.0, bpm = 0.0, playFrom = 0.0;
    int    block = 512;
    std::vector<std::pair<double, std::string>> actions;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--list")           { List(); return 0; }
        else if (a == "--in")        { auto v = next(); if (!v) return Usage(); inPath = v; }
        else if (a == "--rate")      { auto v = next(); if (!v) return Usage(); rate = std::atof(v); }
        else if (a == "--seconds")   { auto v = next(); if (!v) return Usage(); seconds = std::atof(v); }
        else if (a == "--mute-from") { auto v = next(); if (!v) return Usage(); muteFrom = std::atof(v); }
        else if (a == "--bpm")       { auto v = next(); if (!v) return Usage(); bpm = std::atof(v); }
        else if (a == "--play-from") { auto v = next(); if (!v) return Usage(); playFrom = std::atof(v); }
        else if (a == "--block")     { auto v = next(); if (!v) return Usage(); block = std::max(1, std::atoi(v)); }
        else if (a == "--at")
        {
            auto t = next(); auto act = next();
            if (!t || !act) return Usage();
            actions.push_back({ std::atof(t), act });
        }
        else if (!a.empty() && a[0] == '-') return Usage();
        else if (outPath.empty()) outPath = a;
        else return Usage();
    }
    if (outPath.empty() || !(rate >= 8000 && rate <= 384000)) return Usage();

    StereoAudio in;
    if (!inPath.empty())
    {
        std::string error;
        if (!ReadWav(inPath, in, error)) { std::fprintf(stderr, "%s: %s\n", inPath.c_str(), error.c_str()); return 1; }
        rate = in.sampleRate;
        if (seconds < 0) seconds = (double)in.l.size() / rate;
    }
    else if (seconds < 0) seconds = 8.0;

    if (actions.empty())
    {
        actions = { { 0.0, "a_mode=reel" }, { 0.0, "a_mix=1" }, { 0.0, "crossfade=0" },
                    { 0.5, "rec:A" }, { 2.5, "rec:A" } };
        if (muteFrom < 0) muteFrom = 2.5;
    }
    std::vector<ProcStep> steps;
    for (const auto& [t, act] : actions)
    {
        const auto error = Parse(t, act, steps);
        if (!error.empty()) { std::fprintf(stderr, "at %.3f s: %s\n", t, error.c_str()); return 1; }
    }

    TransportAt transport = NoTransport;
    if (bpm > 0)
    {
        transport = PlayingFrom(playFrom, bpm);
        steps.push_back({ playFrom, [](Processor&, Midi&) {} });   // the host starts on a block
    }

    Processor  proc;
    ProcPlayer player(proc, rate, seconds, steps, transport, muteFrom < 0 ? 1e9 : muteFrom);
    if (!inPath.empty()) player.setInput(in.l, in.r);
    while (!player.done()) player.advance((std::size_t)block);

    StereoAudio out;
    out.sampleRate = (uint32_t)rate;
    out.l          = player.out.l;
    out.r          = player.out.r;
    if (!WriteWav(outPath, out)) { std::fprintf(stderr, "%s: could not write\n", outPath.c_str()); return 1; }
    std::printf("%s: %.2f s at %.0f Hz, latency %d samples\n", outPath.c_str(), seconds, rate, proc.latencySamples());
    return 0;
}
