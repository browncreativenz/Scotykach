/**
 * @file hardware.h
 * @brief Desktop stand-in for the firmware's src/hw/hardware.h.
 *
 * The module's panel, as the firmware's CoreUI sees it: the same class,
 * enums and methods, with the electronics replaced by plain state that
 * ScotykachModule sets (pads, switches, CV, gate and clock inputs) and reads
 * (gate outputs, LEDs, outgoing MIDI). The enums are the firmware's, value
 * for value.
 *
 * The build writes this file over the firmware copy's hw/hardware.h (see
 * cmake/Firmware.cmake), so every include of it finds this one.
 *
 * Pots are not read: each pot control is a plugin parameter that
 * ScotykachPanel writes straight to CoreUI, so GetAnalogControlValue()
 * returns 0 and daisy::PotMonitor reports nothing.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "common.h"
#include "daisy_seed.h"
#include "ui/voct.h"

namespace spotykach
{

/** Fixed-capacity FIFO; never allocates after construction. */
template <typename T, size_t kCapacity>
class FixedQueue
{
  public:
    bool push(const T& item)
    {
        if (count == kCapacity) return false;
        items[(head + count) % kCapacity] = item;
        ++count;
        return true;
    }
    bool pop(T& item)
    {
        if (count == 0) return false;
        item = items[head];
        head = (head + 1) % kCapacity;
        --count;
        return true;
    }
    bool   empty() const { return count == 0; }
    size_t size() const  { return count; }
    void   clear()       { head = count = 0; }

  private:
    std::array<T, kCapacity> items {};
    size_t head = 0, count = 0;
};

/** The WS2812 LED chain (infrasonic::Ws2812's interface). Set() and Clear()
 *  draw into a back buffer; Show() publishes it, as sending it down the
 *  chain does on the module. */
class LedChain
{
  public:
    struct Pixel
    {
        uint32_t color      = 0;     // 0xRRGGBB
        float    brightness = 0.f;   // 0..1
    };
    static constexpr size_t kMaxLeds = 128;   // as the firmware's driver

    void Set(uint8_t idx, uint32_t color, float brightness = 1.0f)
    {
        if (idx < kMaxLeds) back[idx] = { color, brightness };
    }
    void Fill(uint32_t color, float brightness = 1.0f) { back.fill({ color, brightness }); }
    void Clear() { back.fill({}); }
    void Show()  { front = back; ++shows; }

    const std::array<Pixel, kMaxLeds>& shown() const { return front; }
    uint64_t showCount() const { return shows; }

  private:
    std::array<Pixel, kMaxLeds> back {}, front {};
    uint64_t shows = 0;
};

/** One MIDI port (libDaisy's MidiUartHandler / MidiUsbHandler interface). */
class MidiPort
{
  public:
    static constexpr size_t kInCapacity  = 256;
    static constexpr size_t kOutCapacity = 256;

    void Listen() {}
    bool HasEvents() const { return !in.empty(); }
    daisy::MidiEvent PopEvent()
    {
        daisy::MidiEvent e;
        in.pop(e);
        return e;
    }
    void EnqueueMessage(const daisy::MidiTxMessage& m) { pending.push(m); }
    void TransmitEnqueuedMessages()
    {
        daisy::MidiTxMessage m;
        while (pending.pop(m)) sent.push(m);
    }

    /** Desktop side: what arrives at the port, and what the firmware sent. */
    bool receive(const daisy::MidiEvent& e) { return in.push(e); }
    bool takeSent(daisy::MidiTxMessage& m)  { return sent.pop(m); }

  private:
    FixedQueue<daisy::MidiEvent, kInCapacity>      in;
    FixedQueue<daisy::MidiTxMessage, kOutCapacity> pending, sent;
};

class Hardware
{
  public:
    // LED indexes in order of chain
    static constexpr uint16_t kNumLedsPerRing = 32;

    enum LedId : uint16_t {
        LED_SPOTY_PAD,
        LED_ALT_A,
        LED_PLAY_A,
        LED_REV_A,

        // Reserved 32 LEDs starting at top of ring,
        // clockwise order - add desired offset (0 - 31)
        LED_RING_A,
        LED_RING_A_LAST = LED_RING_A + kNumLedsPerRing - 1,

        LED_GRIT_A,
        LED_FLUX_A,
        LED_GATE_IN_A,
        LED_CYCLE_A,

        LED_MODE_CENTER,
        LED_MODE_LEFT,
        LED_CLOCK_IN,
        LED_MODE_RIGHT,

        LED_CYCLE_B,
        LED_GATE_IN_B,
        LED_FLUX_B,
        LED_GRIT_B,

        LED_RING_B,
        LED_RING_B_LAST = LED_RING_B + kNumLedsPerRing - 1,

        LED_REV_B,
        LED_PLAY_B,
        LED_FADER_B,
        LED_FADER_A,
        LED_ALT_B,

        LED_LAST
    };

    static constexpr size_t kNumLeds = LED_LAST;

    enum AnalogControlId : uint16_t {
        CTRL_SOS_A,
        CTRL_MODFREQ_A,
        CTRL_MOD_AMT_A,
        CTRL_SIZE_A,
        CTRL_PITCH_A,
        CTRL_POS_A,
        CTRL_ENV_A,

        CTRL_SOS_B,
        CTRL_MODFREQ_B,
        CTRL_MOD_AMT_B,
        CTRL_SIZE_B,
        CTRL_PITCH_B,
        CTRL_POS_B,
        CTRL_ENV_B,

        CTRL_CROSSFADE,

        CTRL_LAST
    };

    static constexpr size_t kNumAnalogControls = CTRL_LAST;

    enum class Pad: uint8_t {
        PlayA   = 0,
        RevA    = 1,
        GritA   = 2,
        FluxA   = 3,
        SeqA    = 4,
        PlayB   = 9,
        RevB    = 8,
        GritB   = 7,
        FluxB   = 6,
        SeqB    = 5,
        Spot    = 10,
        Alt     = 11
    };

    enum CvInputId : uint16_t
    {
        CV_SIZE_POS_A,
        CV_MIX_A,
        CV_V_OCT_A,

        CV_CROSSFADE,

        CV_SIZE_POS_B,
        CV_MIX_B,
        CV_V_OCT_B,

        CV_LAST
    };

    static constexpr size_t kNumCVInputs = CV_LAST;

    Hardware()  = default;
    ~Hardware() = default;

    // ── The firmware's interface ────────────────────────────────────────────
    void Init(float, size_t) {}
    void StartAdcs() {}
    void ProcessAnalogControls() {}
    void ProcessDigitalControls() {}

    /** Delivers pad touches and releases posted since the last call, in
     *  order, as the touch controller's poll does. */
    void ProcessPads();

    inline void SetOnTouch(std::function<void(Pad)> on_touch) { _on_touch = on_touch; }
    inline void SetOnRelease(std::function<void(Pad)> on_release) { _on_release = on_release; }

    float GetAnalogControlValue(AnalogControlId) { return 0.f; }
    float GetControlVoltageValue(CvInputId id) { return cv[id]; }
    inline float GetPotValue(uint16_t pot_id) { return GetAnalogControlValue((AnalogControlId)pot_id); }

    bool GetClockInputState() { return clockIn; }
    bool GetGateInputAState() { return gateIn[0]; }
    bool GetGateInputBState() { return gateIn[1]; }

    inline void SetGateOutA(bool state) { gateOut[0] = state; }
    inline void SetGateOutB(bool state) { gateOut[1] = state; }

    uint32_t GetBootButtonHeldTime() const { return 0; }

    /** The two switch shift registers, raw: a closed switch reads 0, as
     *  with the module's pull-ups. */
    inline uint8_t GetShiftRegState(uint8_t idx) const { return idx < 2 ? shiftReg[idx] : 0; }

    struct Seed { daisy::QSPIHandle qspi; };
    Seed seed;

    LedChain leds;
    MidiPort midi_uart;
    MidiPort midi_usb;

    // ── Desktop side, set and read by ScotykachModule ───────────────────────
    /** Queues a touch (true) or release (false); delivered by ProcessPads. */
    bool postPad(Pad pad, bool touched) { return pads.push({ pad, touched }); }

    std::array<float, kNumCVInputs> cv {};    // -1..1, as the ADC reads them
    bool    clockIn     = false;
    bool    gateIn[2]   = {};
    bool    gateOut[2]  = {};
    uint8_t shiftReg[2] = { 0xFF, 0xFF };      // all switches open

  private:
    struct PadEvent { Pad pad; bool touched; };

    std::function<void(Pad pad)> _on_touch;
    std::function<void(Pad pad)> _on_release;
    FixedQueue<PadEvent, 64> pads;

    Hardware(const Hardware& a)            = delete;
    Hardware& operator=(const Hardware& a) = delete;
};

} // namespace spotykach
