/**
 * @file scenario_dump.cpp
 * @brief Writes one scenario's output to a raw float file.
 *
 *   scenario_dump --list            the scenarios, one name per line
 *   scenario_dump INDEX out.raw     render scenario INDEX
 *
 * Built twice: against scotykach_fw (all patches) and scotykach_fw_single
 * (portability patches only: the firmware as it runs on the module). CTest
 * compares the two files byte for byte, which shows that patches/instances
 * changes nothing about the sound. One scenario per process, because the
 * unpatched firmware supports only one engine per process.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Scenarios.h"

using namespace scotykach::test;

int main(int argc, char** argv)
{
    const auto all = AllScenarios();
    if (argc == 2 && std::string(argv[1]) == "--list")
    {
        for (const auto& s : all) std::printf("%s\n", s.name.c_str());
        return 0;
    }
    if (argc != 3) { std::fprintf(stderr, "usage: scenario_dump --list | INDEX out.raw\n"); return 2; }
    const auto index = (std::size_t)std::atoi(argv[1]);
    if (index >= all.size()) { std::fprintf(stderr, "no scenario %zu\n", index); return 2; }

    const auto r = Run(all[index], 512);
    if (!r.error.empty()) { std::fprintf(stderr, "%s: %s\n", all[index].name.c_str(), r.error.c_str()); return 1; }

    std::FILE* f = std::fopen(argv[2], "wb");
    if (f == nullptr) { std::perror(argv[2]); return 1; }
    std::fwrite(r.l.data(), sizeof(float), r.l.size(), f);
    std::fwrite(r.r.data(), sizeof(float), r.r.size(), f);
    return std::fclose(f) == 0 ? 0 : 1;
}
