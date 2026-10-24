/**
 * @file ScotykachParams.cpp
 * @brief See ScotykachParams.h.
 */

#include "ScotykachParams.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace scotykach::params {

namespace {

const char* const kRouteChoices[]    = { "Stereo", "Double Mono", "Generative Stereo" };
const char* const kModeChoices[]     = { "Reel", "Slice", "Drift" };
const char* const kModTypeAChoices[] = { "Follow", "LFO Square", "LFO Random" };
const char* const kModTypeBChoices[] = { "Follow", "LFO Saw", "LFO Sine" };
const char* const kCvRouteChoices[]  = { "Position", "Both", "Size" };
const char* const kClockChoices[]    = { "Live", "Internal" };
const char* const kAfterRecChoices[] = { "Play", "Overdub" };
const char* const kRecSourceChoices[] = { "Input", "Deck" };
const char* const kOffOn[]           = { "Off", "On" };

struct DeckControlInfo { const char* id; const char* name; float defaultValue; };

// Panel::Control order; defaults from CoreUI::_init_values and the FX.
const DeckControlInfo kDeckControlInfo[kDeckControls] = {
    { "pos",         "Position",        0.f   },
    { "pos_offset",  "Position Offset", 0.f   },
    { "size",        "Size",            1.f   },
    { "pitch",       "Pitch",           .5f   },
    { "env",         "Envelope",        0.f   },
    { "mix",         "Mix",             .5f   },
    { "feedback",    "Feedback",        .95f  },   // kDefaultFeedback
    { "window",      "Window",          .2f   },
    { "env_size",    "Envelope Size",   1.f   },
    { "mod_speed",   "Mod Speed",       .3f   },
    { "mod_amount",  "Mod Amount",      0.f   },
    { "grit_amount", "Grit Intensity",  .2f   },
    { "grit_mix",    "Grit Mix",        .33f  },
    { "grit_char",   "Grit Character",  0.f   },
    { "flux_time",   "Flux Time",       .5f   },
    { "flux_mix",    "Flux Mix",        .7f   },
    { "flux_fb",     "Flux Feedback",   .5f   },
};

// Panel::Global order.
const DeckControlInfo kGlobalInfo[kGlobals] = {
    { "crossfade",    "Crossfade",    .5f },
    { "tempo",        "Tempo",        (120.f - 20.f) / (250.f - 20.f) },   // 120 BPM
    { "key_interval", "Key Interval", .0588f },                            // a quarter note
    { "click",        "Click",        0.f },
    { "pan_speed",    "Pan Speed",    1.f },
    { "pan_range",    "Pan Range",    .6f },
};

// Hardware::CvInputId order.
const DeckControlInfo kCvInfo[kCvInputs] = {
    { "cv_a_size_pos",  "CV A Size/Pos",  0.f },
    { "cv_a_mix",       "CV A Mix",       0.f },
    { "cv_a_voct",      "CV A V/Oct",     0.f },
    { "cv_crossfade",   "CV Crossfade",   0.f },
    { "cv_b_size_pos",  "CV B Size/Pos",  0.f },
    { "cv_b_mix",       "CV B Mix",       0.f },
    { "cv_b_voct",      "CV B V/Oct",     0.f },
};

const char* const kButtonIds[kButtons] = {
    "a_play", "a_rev", "a_grit", "a_flux", "a_seq",
    "b_play", "b_rev", "b_grit", "b_flux", "b_seq",
    "spot", "alt", "tap", "a_gate", "b_gate",
    "a_record", "b_record", "a_clear", "b_clear",
    "a_seq_rec", "b_seq_rec", "a_seq_clear", "b_seq_clear",
};
const char* const kButtonNames[kButtons] = {
    "A Play", "A Rev", "A Grit", "A Flux", "A Seq",
    "B Play", "B Rev", "B Grit", "B Flux", "B Seq",
    "Spot", "Alt", "Tap", "A Gate", "B Gate",
    "A Record", "B Record", "A Clear", "B Clear",
    "A Seq Rec", "B Seq Rec", "A Seq Clear", "B Seq Clear",
};

struct Table
{
    Info        info[kCount];
    std::string ids[kDecks * kDeckControls];
    std::string names[kDecks * kDeckControls];

    Table()
    {
        for (int d = 0; d < kDecks; ++d)
            for (int c = 0; c < kDeckControls; ++c)
            {
                const int p = DeckControl(d, c);
                const char* deck = d == 0 ? "a" : "b";
                ids[p]   = std::string(deck) + "_" + kDeckControlInfo[c].id;
                names[p] = std::string(d == 0 ? "A " : "B ") + kDeckControlInfo[c].name;
                info[p]  = { ids[p].c_str(), names[p].c_str(), Kind::Unit, kDeckControlInfo[c].defaultValue };
            }
        for (int g = 0; g < kGlobals; ++g)
            info[GlobalControl(g)] = { kGlobalInfo[g].id, kGlobalInfo[g].name, Kind::Unit, kGlobalInfo[g].defaultValue };
        for (int v = 0; v < kCvInputs; ++v)
            info[CvInput(v)] = { kCvInfo[v].id, kCvInfo[v].name, Kind::Cv, 0.f };

        auto choice = [&](Switch s, const char* id, const char* name, float def, const char* const* ch, int n) {
            info[SwitchParam(s)] = { id, name, Kind::Choice, def, ch, n };
        };
        choice(kRoute,    "route",      "Route",       0.f, kRouteChoices,    3);
        choice(kModeA,    "a_mode",     "A Mode",      0.f, kModeChoices,     3);   // Reel
        choice(kModeB,    "b_mode",     "B Mode",      0.f, kModeChoices,     3);
        choice(kModTypeA, "a_mod_type", "A Mod Type",  2.f, kModTypeAChoices, 3);   // switch centred
        choice(kModTypeB, "b_mod_type", "B Mod Type",  2.f, kModTypeBChoices, 3);
        choice(kCvRouteA, "a_cv_route", "A CV Route",  1.f, kCvRouteChoices,  3);   // switch centred
        choice(kCvRouteB, "b_cv_route", "B CV Route",  1.f, kCvRouteChoices,  3);
        choice(kClock,    "clock",      "Clock",       0.f, kClockChoices,    2);   // follow Live
        choice(kAfterRecording, "after_rec", "After Recording", 0.f, kAfterRecChoices, 2);   // plain playback
        choice(kRecSourceA, "a_rec_source", "A Record From", 0.f, kRecSourceChoices, 2);
        choice(kRecSourceB, "b_rec_source", "B Record From", 0.f, kRecSourceChoices, 2);
        choice(kGritLockA,  "a_grit_lock",  "A Grit Lock",    0.f, kOffOn, 2);
        choice(kGritLockB,  "b_grit_lock",  "B Grit Lock",    0.f, kOffOn, 2);
        choice(kFluxLockA,  "a_flux_lock",  "A Flux Lock",    0.f, kOffOn, 2);
        choice(kFluxLockB,  "b_flux_lock",  "B Flux Lock",    0.f, kOffOn, 2);
        choice(kQuantiseA,  "a_quantise",   "A Pitch Quantise", 0.f, kOffOn, 2);
        choice(kQuantiseB,  "b_quantise",   "B Pitch Quantise", 0.f, kOffOn, 2);
        choice(kModSyncA,   "a_mod_sync",   "A Mod Sync",     0.f, kOffOn, 2);
        choice(kModSyncB,   "b_mod_sync",   "B Mod Sync",     0.f, kOffOn, 2);
        choice(kFreeSizeA,  "a_free_size",  "A Free Size",    0.f, kOffOn, 2);
        choice(kFreeSizeB,  "b_free_size",  "B Free Size",    0.f, kOffOn, 2);

        for (int b = 0; b < kButtons; ++b)
            info[ButtonParam((Button)b)] = { kButtonIds[b], kButtonNames[b], Kind::Button, 0.f };
    }
};

const Table& TheTable()
{
    static const Table table;
    return table;
}

} // namespace

const Info& Get(int p) { return TheTable().info[std::clamp(p, 0, kCount - 1)]; }

float Clamp(int p, float v)
{
    const Info& i = Get(p);
    switch (i.kind)
    {
        case Kind::Unit:   return std::clamp(v, 0.f, 1.f);
        case Kind::Cv:     return std::clamp(v, -1.f, 1.f);
        case Kind::Choice: return std::clamp(std::round(v), 0.f, (float)(i.numChoices - 1));
        case Kind::Button: return v >= .5f ? 1.f : 0.f;
    }
    return v;
}

} // namespace scotykach::params
