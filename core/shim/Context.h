/**
 * @file Context.h
 * @brief Per-instance state behind the libDaisy stand-ins.
 *
 * The firmware reads time and its memory pool through process-wide calls
 * (daisy::System::GetNow(), SDRAMBuffer::pool()). On the module there is one
 * of everything; in Live, every plugin instance shares one process. So each
 * Engine owns a Context, and makes it current (Scope) around every call into
 * firmware code. The current context is per thread, so instances processing
 * on different threads never see each other's.
 *
 * Time is the engine's sample count at 48 kHz, not the wall clock: holds,
 * timeouts and tap tempo then behave the same in an offline export as in
 * real time, and renders are repeatable.
 */

#pragma once

#include <cstdint>

namespace spotykach { class SDRAMBuffer; }

namespace scotykach::shim {

struct Context
{
    static constexpr uint32_t kSampleRate = 48000;

    uint64_t                samples = 0;        // engine time
    spotykach::SDRAMBuffer* pool    = nullptr;

    uint32_t Ms() const { return (uint32_t)(samples / (kSampleRate / 1000)); }
    uint32_t Us() const { return (uint32_t)(samples * 1000000 / kSampleRate); }

    /** The context of the engine being run on this thread. Outside any Scope
     *  it is a fallback whose time stands still at 0. */
    static Context& Current();

    class Scope
    {
      public:
        explicit Scope(Context& c);
        ~Scope();
        Scope(const Scope&)            = delete;
        Scope& operator=(const Scope&) = delete;

      private:
        Context* previous;
    };
};

} // namespace scotykach::shim
