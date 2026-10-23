/**
 * @file Script.h
 * @brief Text actions on the engine, for the renderer and the tests.
 *
 * Each action does what the module's CoreUI does for that control: a mode
 * change also re-infers the panner and re-applies Size, Pitch goes to
 * set_pitch in Slice mode and set_speed otherwise, Record sets the deck's
 * source first. S1 replaces this with the firmware's own CoreUI.
 *
 *   rec:A  recint:A  play:A  rev:A      pads (recint resamples the other deck)
 *   mode:A=reel|slice|drift             deck mode switch
 *   route=stereo|mono|gen               routing switch
 *   pos:A= size:A= pitch:A= env:A=      pots, 0..1
 *   sos:A= fb:A= win:A= envsize:A=      in/out mix, feedback, window, env size
 *   grit:A=on|off  flux:A=on|off        FX
 *   gritamt:A= fluxamt:A=               FX intensity / time, 0..1
 *   crossfade=  tempo=<bpm>
 *
 * Decks are A or B. Values are knob positions from 0 to 1.
 */

#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "ScotykachEngine.h"

namespace scotykach::script {

/** Knob positions CoreUI remembers and re-applies. */
struct Panel
{
    float size [spotykach::Deck::Count] = { 1.f, 1.f };
    float pitch[spotykach::Deck::Count] = { .5f, .5f };
};

namespace detail {

inline bool ParseDeck(const std::string& s, spotykach::Deck::Ref& ref)
{
    if (s == "A" || s == "a") { ref = spotykach::Deck::A; return true; }
    if (s == "B" || s == "b") { ref = spotykach::Deck::B; return true; }
    return false;
}

inline bool ParseUnit(const std::string& s, float& v)
{
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && v >= 0.f && v <= 1.f;
}

} // namespace detail

/** Applies one action. Returns an empty string, or what was wrong with it. */
inline std::string Apply(Engine& engine, Panel& panel, const std::string& action)
{
    using namespace spotykach;
    using detail::ParseDeck;
    using detail::ParseUnit;

    const auto eq    = action.find('=');
    const auto lhs   = action.substr(0, eq);
    const auto value = eq == std::string::npos ? std::string() : action.substr(eq + 1);
    const auto colon = lhs.find(':');
    const auto name  = lhs.substr(0, colon);
    Deck::Ref  ref   = Deck::None;
    if (colon != std::string::npos && !ParseDeck(lhs.substr(colon + 1), ref))
        return "unknown deck in '" + action + "' (use A or B)";

    const bool  needsDeck = !(name == "route" || name == "crossfade" || name == "tempo");
    if (needsDeck && ref == Deck::None) return "'" + action + "' needs a deck, as in " + name + ":A";

    const bool  pad  = name == "rec" || name == "recint" || name == "play" || name == "rev";
    if (pad != value.empty()) return pad ? "'" + name + "' takes no value" : "'" + action + "' needs a value";

    float unit = 0.f;
    const bool isUnit = ParseUnit(value, unit);

    std::string error;
    engine.control([&](Core& core) {
        auto deck = [&]() -> Deck& { return core.deck(ref); };

        if (name == "rec" || name == "recint")
        {
            core.set_source(name == "rec" ? Deck::Source::external : Deck::Source::internal, ref);
            deck().toggle_recording();
        }
        else if (name == "play") deck().toggle_play();
        else if (name == "rev")  deck().set_reverse(!deck().is_reverse());
        else if (name == "mode")
        {
            Mode m;
            if      (value == "reel")  m = Mode::Reel;
            else if (value == "slice") m = Mode::Slice;
            else if (value == "drift") m = Mode::Drift;
            else { error = "mode is reel, slice or drift"; return; }
            deck().set_mode(m);
            core.infer_panner_mode();
            deck().voxs().set_size(panel.size[ref], false);
        }
        else if (name == "route")
        {
            if      (value == "stereo") core.set_route(Route::Stereo);
            else if (value == "mono")   core.set_route(Route::DoubleMono);
            else if (value == "gen")    core.set_route(Route::GenerativeStereo);
            else error = "route is stereo, mono or gen";
        }
        else if (name == "grit" || name == "flux")
        {
            if (value != "on" && value != "off") { error = name + " is on or off"; return; }
            if (name == "grit") deck().fx().set_grit_on(value == "on");
            else                deck().fx().set_flux_on(value == "on");
        }
        else if (name == "tempo")
        {
            const float bpm = std::strtof(value.c_str(), nullptr);
            if (!(bpm >= 20.f && bpm <= 300.f)) { error = "tempo is in BPM"; return; }
            core.driver().set_tempo_norm(Tempo::abs_to_norm(bpm));
        }
        else if (!isUnit) error = "'" + action + "': value must be from 0 to 1";
        else if (name == "pos")       deck().voxs().set_start(unit);
        else if (name == "size")      { panel.size[ref] = unit; deck().voxs().set_size(unit, false); }
        else if (name == "pitch")
        {
            panel.pitch[ref] = unit;
            if (deck().mode() == Mode::Slice) deck().voxs().set_pitch(unit);
            else                              deck().voxs().set_speed(unit);
        }
        else if (name == "env")       deck().voxs().set_shape(unit);
        else if (name == "envsize")   deck().voxs().set_env_size(unit);
        else if (name == "win")       deck().voxs().set_win_size(unit);
        else if (name == "sos")       deck().set_inout_mix(unit);
        else if (name == "fb")        deck().set_feedback(unit);
        else if (name == "gritamt")   deck().fx().grit().set_intensity(unit);
        else if (name == "fluxamt")   deck().fx().flux().set_time_norm(unit);
        else if (name == "crossfade") core.set_mix(unit);
        else error = "unknown action '" + action + "'";
    });
    return error;
}

struct Step
{
    double      time;     // seconds
    std::string action;
};

/** Plays a script against an engine: renders n samples of input, applying
 *  each step once the render reaches its time. A block is split at a step,
 *  so a step lands at the same sample whatever the host block size. */
class Player
{
  public:
    Player(Engine& e, Panel& p, std::vector<Step> s,
           const float* inL, const float* inR, float* outL, float* outR, std::size_t n)
        : engine(e), panel(p), steps(std::move(s)),
          inL(inL), inR(inR), outL(outL), outR(outR), n(n)
    {
        std::stable_sort(steps.begin(), steps.end(),
                         [](const Step& x, const Step& y) { return x.time < y.time; });
    }

    bool done() const { return pos >= n; }

    /** Renders up to `block` samples. Returns an empty string, or the first
     *  bad step. */
    std::string advance(std::size_t block)
    {
        for (; next < steps.size() && At(steps[next].time) <= pos; ++next)
        {
            const auto error = Apply(engine, panel, steps[next].action);
            if (!error.empty()) return "at " + std::to_string(steps[next].time) + " s: " + error;
        }
        std::size_t len = std::min(block, n - pos);
        if (next < steps.size()) len = std::min(len, At(steps[next].time) - pos);
        engine.process(inL + pos, inR + pos, outL + pos, outR + pos, len);
        pos += len;
        return {};
    }

  private:
    static std::size_t At(double t) { return (std::size_t)(t * Engine::kSampleRate); }

    Engine&           engine;
    Panel&            panel;
    std::vector<Step> steps;
    const float*      inL;
    const float*      inR;
    float*            outL;
    float*            outR;
    std::size_t       n;
    std::size_t       pos  = 0;
    std::size_t       next = 0;
};

/** Renders all n samples in host blocks of `block` samples. Returns an
 *  empty string, or the first bad step. */
inline std::string Render(Engine& engine, Panel& panel, std::vector<Step> steps,
                          const float* inL, const float* inR, float* outL, float* outR,
                          std::size_t n, std::size_t block)
{
    Player player(engine, panel, std::move(steps), inL, inR, outL, outR, n);
    while (!player.done())
    {
        const auto error = player.advance(block);
        if (!error.empty()) return error;
    }
    return {};
}

} // namespace scotykach::script
