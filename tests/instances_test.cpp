/**
 * @file instances_test.cpp
 * @brief Several engines in one process, as plugin instances in Live: each
 *        sounds exactly as it does alone, whether they take turns on one
 *        thread or run on separate threads at once.
 *
 * Fails without patches/instances: the firmware's Core::init kept its buffer
 * tables in function statics, so the second engine wrote into the first one's
 * loops.
 */

#include <cstdio>
#include <thread>

#include "Check.h"
#include "Scenarios.h"

using namespace scotykach;
using namespace scotykach::test;

namespace {

std::size_t FirstDifference(const Render& a, const Render& b)
{
    for (std::size_t i = 0; i < a.l.size(); ++i)
        if (a.l[i] != b.l[i] || a.r[i] != b.r[i]) return i;
    return a.l.size();
}

const Scenario kFirst  = Loop("reel");
const Scenario kSecond = Performance("slice", "mono");

/** Two engines created together, advancing in turn with different block
 *  sizes. */
void TestTakingTurns(const Render& soloFirst, const Render& soloSecond)
{
    Engine e1, e2;
    const auto in1 = Input(kFirst), in2 = Input(kSecond);
    Render     out1 { std::vector<float>(in1.l.size()), std::vector<float>(in1.l.size()), {} };
    Render     out2 { std::vector<float>(in2.l.size()), std::vector<float>(in2.l.size()), {} };

    script::Panel  p1, p2;
    script::Player a(e1, p1, kFirst.steps,  in1.l.data(), in1.r.data(), out1.l.data(), out1.r.data(), in1.l.size());
    script::Player b(e2, p2, kSecond.steps, in2.l.data(), in2.r.data(), out2.l.data(), out2.r.data(), in2.l.size());
    while (!a.done() || !b.done())
    {
        if (!a.done()) { const auto err = a.advance(300); CHECK(err.empty(), "%s", err.c_str()); }
        if (!b.done()) { const auto err = b.advance(128); CHECK(err.empty(), "%s", err.c_str()); }
    }

    const auto d1 = FirstDifference(out1, soloFirst);
    const auto d2 = FirstDifference(out2, soloSecond);
    CHECK(d1 == out1.l.size(), "taking turns: first engine differs from its solo render at %zu", d1);
    CHECK(d2 == out2.l.size(), "taking turns: second engine differs from its solo render at %zu", d2);
}

/** Two engines on two threads at once. */
void TestThreads(const Render& soloFirst, const Render& soloSecond)
{
    Render out1, out2;
    std::thread t1([&] { out1 = Run(kFirst, 256); });
    std::thread t2([&] { out2 = Run(kSecond, 256); });
    t1.join();
    t2.join();

    const auto d1 = FirstDifference(out1, soloFirst);
    const auto d2 = FirstDifference(out2, soloSecond);
    CHECK(d1 == out1.l.size(), "threads: first engine differs from its solo render at %zu", d1);
    CHECK(d2 == out2.l.size(), "threads: second engine differs from its solo render at %zu", d2);
}

} // namespace

int main()
{
    std::printf("instances_test\n");
    const auto soloFirst  = Run(kFirst, 256);
    const auto soloSecond = Run(kSecond, 256);
    CHECK(soloFirst.error.empty() && soloSecond.error.empty(), "scenario error");
    TestTakingTurns(soloFirst, soloSecond);
    TestThreads(soloFirst, soloSecond);
    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
