/**
 * @file params_test.cpp
 * @brief The parameter table: unique IDs, the module's factory values as
 *        defaults, and a processor that starts out matching them.
 */

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "Check.h"
#include "ScotykachModule.h"
#include "ScotykachParams.h"
#include "ScotykachProcessor.h"

using namespace scotykach;
using namespace scotykach::params;
using spotykach::Core;
using spotykach::Deck;

int main()
{
    std::printf("params_test\n");

    std::set<std::string> ids;
    for (int p = 0; p < kCount; ++p)
    {
        const auto& i = Get(p);
        CHECK(ids.insert(i.id).second, "duplicate ID %s", i.id);
        CHECK(i.name && *i.name, "parameter %d has no name", p);
        if (i.kind == Kind::Choice)
            CHECK(i.numChoices > 1 && i.defaultValue < i.numChoices, "%s: choices", i.id);
    }
    std::printf("  %d parameters\n", kCount);

    // The pot controls' defaults are what the firmware boots with.
    Module m;
    m.control([](Panel& panel, Core&) {
        for (int d = 0; d < kDecks; ++d)
            for (int c = 0; c < kDeckControls; ++c)
            {
                const float fw = panel.get(d == 0 ? Deck::A : Deck::B, (Panel::Control)c);
                const auto& i  = Get(DeckControl(d, c));
                CHECK(std::abs(fw - i.defaultValue) < 1e-6f, "%s: default %.6f, firmware %.6f", i.id, i.defaultValue, fw);
            }
        for (int g = 0; g < kGlobals; ++g)
        {
            const float fw = panel.get((Panel::Global)g);
            const auto& i  = Get(GlobalControl(g));
            CHECK(std::abs(fw - i.defaultValue) < 1e-6f, "%s: default %.6f, firmware %.6f", i.id, i.defaultValue, fw);
        }
    });

    CHECK(Clamp(SwitchParam(kModeA), 7.f) == 2.f, "choices clamp to the last");
    CHECK(Clamp(ButtonParam(kAlt), .7f) == 1.f, "buttons round");
    CHECK(Clamp(CvInput(0), -3.f) == -1.f, "CV clamps to -1");

    // A fresh processor reports the defaults, and nothing changed.
    Processor proc;
    for (int p = 0; p < kCount; ++p)
    {
        CHECK(proc.getParameter(p) == Get(p).defaultValue, "%s starts at %.4f", Get(p).id, proc.getParameter(p));
        CHECK(!proc.takeChanged(p), "%s reports a change before any processing", Get(p).id);
    }

    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
