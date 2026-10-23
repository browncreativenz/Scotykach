/**
 * @file daisy_seed.h
 * @brief The parts of libDaisy that Spotykach's engine uses, for a desktop.
 *
 * Only what src/core needs: System time, StopwatchTimer, ScopedIrqBlocker,
 * Logger and DSY_SDRAM_BSS. Time comes from the current engine's sample
 * clock (Context.h).
 */

#pragma once

#include <cstdarg>
#include <cstdint>

#include "Context.h"

#define DSY_SDRAM_BSS

namespace daisy {

struct System
{
    static uint32_t GetNow() { return scotykach::shim::Context::Current().Ms(); }
    static uint32_t GetUs()  { return scotykach::shim::Context::Current().Us(); }
};

/** libDaisy's StopwatchTimer, timed by the engine's sample clock. */
class StopwatchTimer
{
  public:
    void     Init()                           { Restart(); }
    void     Restart()                        { start = System::GetUs(); }
    uint32_t GetElapsedUs() const             { return System::GetUs() - start; }
    uint32_t GetElapsedMs() const             { return GetElapsedUs() / 1000; }
    bool     HasPassedMs(uint32_t ms) const   { return GetElapsedMs() >= ms; }
    bool     HasPassedUs(uint32_t us) const   { return GetElapsedUs() >= us; }

  private:
    uint32_t start = 0;
};

/** Interrupts don't exist here: every firmware call for an instance runs on
 *  one thread, in order, so there is nothing to block. */
struct ScopedIrqBlocker {};

enum LoggerDestination { LOGGER_NONE, LOGGER_INTERNAL, LOGGER_EXTERNAL };

template <LoggerDestination>
struct Logger
{
    static void Print(const char*, ...) {}
    static void PrintLine(const char*, ...) {}
    static void PrintLineV(const char*, va_list) {}
    static void StartLog(bool = false) {}
};

} // namespace daisy
