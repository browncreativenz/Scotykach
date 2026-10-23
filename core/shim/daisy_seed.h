/**
 * @file daisy_seed.h
 * @brief The parts of libDaisy that the Spotykach firmware uses, for a desktop.
 *
 * Only what src/core, src/ui and src/memory need: System time,
 * StopwatchTimer, ScopedIrqBlocker, Logger, DSY_SDRAM_BSS, the UI event
 * queue and pot monitor, MIDI message types, and PersistentStorage. Names
 * and signatures follow libDaisy (the bleeptools fork the firmware pins).
 * Time comes from the current engine's sample clock (Context.h).
 */

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <deque>

#include "Context.h"

#define DSY_SDRAM_BSS

namespace daisy {

struct System
{
    static uint32_t GetNow() { return scotykach::shim::Context::Current().Ms(); }
    static uint32_t GetUs()  { return scotykach::shim::Context::Current().Us(); }

    /** Ticks run at 1 MHz here, so a tick is a microsecond of engine time. */
    static uint32_t GetTick()     { return GetUs(); }
    static uint32_t GetTickFreq() { return 1000000; }
    static uint32_t GetUsBetweenTicks(uint32_t current, uint32_t last) { return current - last; }

    /** Only the CV calibration routine waits; engine time can't pass inside
     *  a call, so this returns at once. */
    static void Delay(uint32_t) {}
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

// ── UI ──────────────────────────────────────────────────────────────────────

/** libDaisy's UiEventQueue, for the one event type the firmware reads. */
class UiEventQueue
{
  public:
    struct Event
    {
        enum class EventType { invalid, potMoved };
        EventType type = EventType::invalid;
        struct { uint16_t id; float newPosition; } asPotMoved {};
    };

    void AddPotMoved(uint16_t potId, float newPosition)
    {
        Event e;
        e.type                   = Event::EventType::potMoved;
        e.asPotMoved.id          = potId;
        e.asPotMoved.newPosition = newPosition;
        events.push_back(e);
    }
    Event GetAndRemoveNextEvent()
    {
        if (events.empty()) return {};
        Event e = events.front();
        events.pop_front();
        return e;
    }
    bool IsQueueEmpty() const { return events.empty(); }

  private:
    std::deque<Event> events;
};

/** The pots are replaced by one plugin parameter per control (see
 *  ScotykachPanel), so the monitor never reports a movement. */
template <typename Backend, size_t numPots>
class PotMonitor
{
  public:
    void Init(UiEventQueue&, Backend&, uint16_t = 0, float = 0.f, float = 0.f) {}
    void Process() {}
};

// ── MIDI ────────────────────────────────────────────────────────────────────

enum MidiMessageType
{
    NoteOff, NoteOn, PolyphonicKeyPressure, ControlChange, ProgramChange,
    ChannelPressure, PitchBend, SystemCommon, SystemRealTime, ChannelMode, MessageLast,
};

enum SystemRealTimeType
{
    TimingClock, SRTUndefined0, Start, Continue, Stop, SRTUndefined1, ActiveSensing,
    Reset, SystemRealTimeLast,
};

struct NoteOnEvent        { int channel; uint8_t note; uint8_t velocity; };
struct ControlChangeEvent { int channel; uint8_t control_number; uint8_t value; };

struct MidiEvent
{
    MidiMessageType    type     = MessageLast;
    int                channel  = 0;
    uint8_t            data[2]  = {};
    SystemRealTimeType srt_type = SystemRealTimeLast;

    NoteOnEvent        AsNoteOn() const        { return { channel, data[0], data[1] }; }
    ControlChangeEvent AsControlChange() const { return { channel, data[0], data[1] }; }
};

/** An outgoing message: raw bytes, as in the bleeptools fork. */
struct MidiTxMessage
{
    uint8_t data[3] = {};
    size_t  size    = 0;

    static MidiTxMessage SystemRealtimeClock() { return Byte(0xF8); }
    static MidiTxMessage SystemRealtimeStart() { return Byte(0xFA); }
    static MidiTxMessage SystemRealtimeStop()  { return Byte(0xFC); }

  private:
    static MidiTxMessage Byte(uint8_t b) { MidiTxMessage m; m.data[0] = b; m.size = 1; return m; }
};

// ── Persistent storage ──────────────────────────────────────────────────────

/** The QSPI flash only holds the CV calibration here; nothing is stored. */
struct QSPIHandle {};

/** libDaisy's PersistentStorage (the fork's version), in memory: it starts
 *  from the factory defaults, as a module with nothing saved does. */
template <typename SettingStruct, const char* Slug, uint32_t Version>
class PersistentStorage
{
  public:
    enum class State { UNKNOWN = 0, FACTORY = 1, USER = 2 };

    void Init(const QSPIHandle&, const SettingStruct& defaults, uint32_t = 0)
    {
        settings = defaults;
        state    = State::FACTORY;
    }
    State          GetState() const { return state; }
    SettingStruct& GetSettings()    { return settings; }
    void Save()                              { state = State::USER; }
    void Save(const SettingStruct& s)        { settings = s; state = State::USER; }

  private:
    SettingStruct settings {};
    State         state = State::UNKNOWN;
};

} // namespace daisy
