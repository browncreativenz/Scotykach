/**
 * @file ScotykachPanel.cpp
 * @brief See ScotykachPanel.h.
 */

#include "ScotykachPanel.h"

#include <algorithm>

#include "ui/core.ui.h"

namespace scotykach {

using namespace spotykach;

namespace {

float Clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

/** MValue::value() clears the apply flag; this reads without consuming. */
float Peek(MValue& v)
{
    const bool pending = v.apply();
    const float x = v.value();
    if (pending) v.set_apply();
    return x;
}

// Shift-register bits, as CoreUI::_process_switches decodes them.
namespace sr1 {
constexpr int kModALfo1 = 0, kModAFollow = 1, kRouteGen = 2, kRouteMono = 3,
              kCvASize = 4, kCvAPos = 5, kModeADrift = 6, kModeAReel = 7;
}
namespace sr2 {
constexpr int kCvBSize = 0, kCvBPos = 1, kModeBDrift = 2, kModeBReel = 3,
              kModBLfo1 = 4, kModBFollow = 5, kTap = 6;
}

} // namespace

Panel::Panel(CoreUI& u, Core& c, Hardware& h) : ui(u), core(c), hw(h)
{
    writeSwitches();
}

/* The CoreUI value behind each control, as CoreUI::_on_midi_cc names them.
 * Size and Pitch are listed apart: setting them does a little more. */
#define SCOTYKACH_PLAIN_VALUES(X)                                     \
    X(Position, _pos) X(PositionOffset, _pos_offset)                  \
    X(Envelope, _env) X(Mix, _mix)                                    \
    X(Feedback, _feedback) X(Window, _win) X(EnvelopeSize, _env_size) \
    X(ModSpeed, _mod_speed) X(ModAmount, _mod_amp)                    \
    X(GritIntensity, _grit_intens) X(GritMix, _grit_mix)              \
    X(GritCharacter, _grit_char) X(FluxTime, _flux_intens)            \
    X(FluxMix, _flux_mix) X(FluxFeedback, _flux_fb)
#define SCOTYKACH_CONTROL_VALUES(X) \
    SCOTYKACH_PLAIN_VALUES(X) X(Size, _size) X(Pitch, _speed)

void Panel::set(Deck::Ref ref, Control c, float value)
{
    const float v    = Clamp01(value);
    auto&       deck = core.deck(ref);
    MValue*     mv   = nullptr;

    switch (c)
    {
        case Control::Size:
            // CoreUI::_process_ui_queue, CTRL_SIZE: in Slice mode with Tap
            // held, Size picks a length in quarters and fits the tempo to it.
            if (deck.mode() == Mode::Slice)
            {
                if (ui._tap_hold.passed() && !deck.is_empty())
                {
                    mv = &ui._size_quarters[ref];
                    mv->set(v);
                    ui._set_tempo_by_size(ref, v);
                }
                else
                {
                    mv = &ui._size[ref];
                    mv->set(v);
                    ui._size_quarters[ref].set(v);
                }
            }
            else
            {
                mv = &ui._size[ref];
                mv->set(v);
            }
            break;

        case Control::Pitch:
            // With Alt held, CoreUI snaps the speed to its steps.
            mv = &ui._speed[ref];
            mv->set(v);
            ui._pitch_quantized.set(ref, ui._touched.test(CoreUI::Alt));
            break;

#define SCOTYKACH_SET(name, member) \
        case Control::name: mv = &ui.member[ref]; mv->set(v); break;
        SCOTYKACH_PLAIN_VALUES(SCOTYKACH_SET)
#undef SCOTYKACH_SET

        case Control::Count: return;
    }

    // As a pot does: the deck's ring shows the value for a moment.
    ui._changing_value_id[ref] = mv->id();
    ui._value_display_timeout.start();
}

float Panel::get(Deck::Ref ref, Control c) const
{
    switch (c)
    {
#define SCOTYKACH_GET(name, member) \
        case Control::name: return Peek(ui.member[ref]);
        SCOTYKACH_CONTROL_VALUES(SCOTYKACH_GET)
#undef SCOTYKACH_GET
        case Control::Count: break;
    }
    return 0.f;
}

#undef SCOTYKACH_CONTROL_VALUES
#undef SCOTYKACH_PLAIN_VALUES

void Panel::set(Global g, float value)
{
    const float v = Clamp01(value);
    MValue*     mv  = nullptr;
    Deck::Ref   ref = Deck::A;   // which deck's ring shows it, as on the module

    switch (g)
    {
        case Global::Crossfade:
            crossfade = v;
            core.set_mix(v);
            return;
        case Global::Tempo:       mv = &ui._tempo;        break;
        case Global::KeyInterval: mv = &ui._key_interval; break;
        case Global::Click:       mv = &ui._click_mix;    break;
        case Global::PanSpeed:    mv = &ui._pan_speed;    ref = Deck::B; break;
        case Global::PanRange:    mv = &ui._pan_range;    ref = Deck::B; break;
        case Global::Count:       return;
    }
    mv->set(v);
    ui._changing_value_id[ref] = mv->id();
    ui._value_display_timeout.start();
}

float Panel::get(Global g) const
{
    switch (g)
    {
        case Global::Crossfade:   return crossfade;
        case Global::Tempo:       return Peek(ui._tempo);
        case Global::KeyInterval: return Peek(ui._key_interval);
        case Global::Click:       return Peek(ui._click_mix);
        case Global::PanSpeed:    return Peek(ui._pan_speed);
        case Global::PanRange:    return Peek(ui._pan_range);
        case Global::Count:       break;
    }
    return 0.f;
}

// ── Switches ────────────────────────────────────────────────────────────────

void Panel::setSwitch(int reg, int bit, bool closed)
{
    if (closed) switches[reg] = (uint8_t)(switches[reg] |  (1u << bit));
    else        switches[reg] = (uint8_t)(switches[reg] & ~(1u << bit));
}

void Panel::writeSwitches()
{
    // A closed switch pulls its input low.
    hw.shiftReg[0] = (uint8_t)~switches[0];
    hw.shiftReg[1] = (uint8_t)~switches[1];
}

void Panel::setRoute(Route r)
{
    setSwitch(0, sr1::kRouteGen,  r == Route::GenerativeStereo);
    setSwitch(0, sr1::kRouteMono, r == Route::DoubleMono);
    writeSwitches();
}

void Panel::setMode(Deck::Ref ref, Mode m)
{
    const int reg   = ref == Deck::A ? 0 : 1;
    const int drift = ref == Deck::A ? sr1::kModeADrift : sr2::kModeBDrift;
    const int reel  = ref == Deck::A ? sr1::kModeAReel  : sr2::kModeBReel;
    setSwitch(reg, drift, m == Mode::Drift);   // Slice is neither
    setSwitch(reg, reel,  m == Mode::Reel);
    writeSwitches();
}

void Panel::setModType(Deck::Ref ref, ModType t)
{
    const int reg    = ref == Deck::A ? 0 : 1;
    const int follow = ref == Deck::A ? sr1::kModAFollow : sr2::kModBFollow;
    const int lfo1   = ref == Deck::A ? sr1::kModALfo1   : sr2::kModBLfo1;
    setSwitch(reg, follow, t == ModType::Follow);
    setSwitch(reg, lfo1,   t == ModType::Lfo1);   // Lfo2 is neither
    writeSwitches();
}

void Panel::setCvRoute(Deck::Ref ref, CvRoute r)
{
    const int reg  = ref == Deck::A ? 0 : 1;
    const int pos  = ref == Deck::A ? sr1::kCvAPos  : sr2::kCvBPos;
    const int size = ref == Deck::A ? sr1::kCvASize : sr2::kCvBSize;
    setSwitch(reg, pos,  r == CvRoute::Position);   // Both is neither
    setSwitch(reg, size, r == CvRoute::Size);
    writeSwitches();
}

void Panel::setTap(bool held)
{
    setSwitch(1, sr2::kTap, held);
    writeSwitches();
}

// ── Pads, inputs, MIDI ──────────────────────────────────────────────────────

void Panel::pad(Pad p, bool touched) { hw.postPad(p, touched); }

void Panel::setCv(Hardware::CvInputId id, float value)
{
    if (id < Hardware::kNumCVInputs) hw.cv[id] = std::clamp(value, -1.f, 1.f);
}

void Panel::setGate(Deck::Ref ref, bool high) { hw.gateIn[ref == Deck::A ? 0 : 1] = high; }
void Panel::setClockIn(bool high)             { hw.clockIn = high; }

bool Panel::midiIn(uint8_t status, uint8_t data1, uint8_t data2)
{
    daisy::MidiEvent e;
    switch (status)
    {
        case 0xF8: e.srt_type = daisy::TimingClock; break;
        case 0xFA: e.srt_type = daisy::Start;       break;
        case 0xFB: e.srt_type = daisy::Continue;    break;
        case 0xFC: e.srt_type = daisy::Stop;        break;
        default:   break;
    }
    if (status >= 0xF8)
    {
        if (e.srt_type == daisy::SystemRealTimeLast) return false;
        e.type = daisy::SystemRealTime;
        return hw.midi_usb.receive(e);
    }

    e.channel = status & 0x0F;
    e.data[0] = data1 & 0x7F;
    e.data[1] = data2 & 0x7F;
    switch (status & 0xF0)
    {
        // As libDaisy's parser: a note-on at velocity 0 is a note-off.
        case 0x90: e.type = e.data[1] == 0 ? daisy::NoteOff : daisy::NoteOn; break;
        case 0x80: e.type = daisy::NoteOff;       break;
        case 0xB0: e.type = daisy::ControlChange; break;
        default:   return false;
    }
    return hw.midi_usb.receive(e);
}

void Panel::setClockSource(Driver::Source source)
{
    // Alt with Tap held steps through the sources (CoreUI::_on_alt_touch).
    auto& d = core.driver();
    for (int i = 0; i < 3 && d.source() != source; ++i)
    {
        d.toggle_source();
        ui._clock_source_changed = true;
        ui._value_display_timeout.start();
    }
}

void Panel::setInternalTempo(float bpm)
{
    // Tempo::set_norm clamps to the firmware's range.
    core.driver().set_tempo_norm(Tempo::abs_to_norm(bpm));
}

void Panel::finishBoot()
{
    for (int i = 0; i < 100000 && ui._state == CoreUI::State::launching; ++i)
        ui.render_leds();
}

bool Panel::isBooted() const { return ui._state == CoreUI::State::ready; }

} // namespace scotykach
