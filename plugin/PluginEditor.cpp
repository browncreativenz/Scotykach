/**
 * @file PluginEditor.cpp
 * @brief See PluginEditor.h.
 */

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include "hw/hardware.h"

using HW        = spotykach::Hardware;
using DeckState  = scotykach::Processor::DeckState;

namespace {

constexpr int kPadW = 72, kPadH = 46, kGap = 8, kRing = 92, kMargin = 12;
constexpr int kTop  = 2 * (kRing + 26) + 46;   // the pads area, above the parameter list

const juce::Colour kBackground { 0xff1b1b1e };
const juce::Colour kPadOff     { 0xff2e2e33 };
const juce::Colour kText       { 0xffe8e8ea };
const juce::Colour kDim        { 0xff8a8a92 };

/** An LED colour (brightness already applied) as a fill over a dark pad. */
juce::Colour LedFill(uint32_t rgb)
{
    const int r = (int)(rgb >> 16) & 0xFF, g = (int)(rgb >> 8) & 0xFF, b = (int)rgb & 0xFF;
    const int m = std::max({ r, g, b });
    if (m == 0) return juce::Colours::transparentBlack;
    return juce::Colour((uint8_t)(r * 255 / m), (uint8_t)(g * 255 / m), (uint8_t)(b * 255 / m))
        .withAlpha((float)m / 255.f);
}

struct PadSpec { const char* id; const char* text; int led; bool latching; };

const PadSpec kPadSpecs[] = {
    { "a_play", "Play", HW::LED_PLAY_A, false },
    { "a_rev",  "Rev",  HW::LED_REV_A,  false },
    { "a_grit", "Grit", HW::LED_GRIT_A, false },
    { "a_flux", "Flux", HW::LED_FLUX_A, false },
    { "a_seq",  "Seq",  -1,             false },
    { "b_play", "Play", HW::LED_PLAY_B, false },
    { "b_rev",  "Rev",  HW::LED_REV_B,  false },
    { "b_grit", "Grit", HW::LED_GRIT_B, false },
    { "b_flux", "Flux", HW::LED_FLUX_B, false },
    { "b_seq",  "Seq",  -1,             false },
    { "alt",    "Alt",  HW::LED_ALT_A,  true  },
    { "tap",    "Tap",  HW::LED_CLOCK_IN, false },
    { "spot",   "Spot", -1,             false },
};

} // namespace

// ── Pad ─────────────────────────────────────────────────────────────────────

ScotykachEditor::Pad::Pad(juce::RangedAudioParameter& p, juce::String t, int l, bool latch)
    : param(p), text(std::move(t)), led(l), latching(latch)
{
    setRepaintsOnMouseActivity(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ScotykachEditor::Pad::press()
{
    if (latching)
    {
        param.beginChangeGesture();
        param.setValueNotifyingHost(isOn() ? 0.f : 1.f);
        param.endChangeGesture();
    }
    else if (!down)
    {
        down = true;
        param.beginChangeGesture();
        param.setValueNotifyingHost(1.f);
    }
    repaint();
}

void ScotykachEditor::Pad::release()
{
    if (latching || !down) return;
    down = false;
    param.setValueNotifyingHost(0.f);
    param.endChangeGesture();
    repaint();
}

void ScotykachEditor::Pad::mouseDown(const juce::MouseEvent&) { press(); }
void ScotykachEditor::Pad::mouseUp(const juce::MouseEvent&)   { release(); }

void ScotykachEditor::Pad::refresh(uint32_t ledColour)
{
    const uint32_t c = led >= 0 ? ledColour : 0;
    if (c == colour && isOn() == shownOn) return;
    colour  = c;
    shownOn = isOn();
    repaint();
}

void ScotykachEditor::Pad::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(kPadOff);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(LedFill(colour));
    g.fillRoundedRectangle(r, 7.f);

    const bool on = isOn();
    g.setColour(on ? kText : kText.withAlpha(.18f));
    g.drawRoundedRectangle(r, 7.f, on ? 2.5f : 1.f);

    const auto fill = LedFill(colour);
    g.setColour(fill.getPerceivedBrightness() * fill.getFloatAlpha() > .55f ? kBackground : kText);
    g.setFont(juce::FontOptions(15.f, juce::Font::bold));
    g.drawText(text, getLocalBounds(), juce::Justification::centred);
}

// ── Ring ────────────────────────────────────────────────────────────────────

/** A deck's 32-LED ring, clockwise from the top. */
class ScotykachEditor::Ring : public juce::Component
{
  public:
    explicit Ring(int first) : firstLed(first) { setInterceptsMouseClicks(false, false); }

    void refresh(const scotykach::Processor& p)
    {
        bool changed = false;
        for (int i = 0; i < HW::kNumLedsPerRing; ++i)
        {
            const uint32_t c = p.led(firstLed + i);
            changed |= c != colours[(size_t)i];
            colours[(size_t)i] = c;
        }
        if (changed) repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto  c = getLocalBounds().toFloat().getCentre();
        const float r = (float)std::min(getWidth(), getHeight()) * .5f - 6.f;
        for (int i = 0; i < HW::kNumLedsPerRing; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float)i / (float)HW::kNumLedsPerRing;
            const juce::Point<float> at { c.x + r * std::sin(a), c.y - r * std::cos(a) };
            const auto dot = juce::Rectangle<float>(7.f, 7.f).withCentre(at);
            g.setColour(kPadOff);
            g.fillEllipse(dot);
            g.setColour(LedFill(colours[(size_t)i]));
            g.fillEllipse(dot);
        }
    }

  private:
    int firstLed;
    std::array<uint32_t, HW::kNumLedsPerRing> colours {};
};

// ── Editor ──────────────────────────────────────────────────────────────────

ScotykachEditor::ScotykachEditor(ScotykachAudioProcessor& p)
    : AudioProcessorEditor(p), plugin(p)
{
    for (const auto& s : kPadSpecs)
    {
        auto* param = p.parameters.getParameter(s.id);
        jassert(param != nullptr);
        pads.push_back(std::make_unique<Pad>(*param, s.text, s.led, s.latching));
        padIds.push_back(s.id);
        addAndMakeVisible(*pads.back());
    }

    for (int d = 0; d < 2; ++d)
    {
        rings[d] = std::make_unique<Ring>(d == 0 ? HW::LED_RING_A : HW::LED_RING_B);
        addAndMakeVisible(*rings[d]);
        status[d].setColour(juce::Label::textColourId, kText);
        status[d].setFont(juce::FontOptions(14.f));
        addAndMakeVisible(status[d]);
    }

    hint.setText("Pads press while the mouse is down; Alt stays on until clicked again.  "
                 "Record: Alt on, Play arms the deck (it starts at the next sound, or beat in Slice). "
                 "Play again closes the loop, which plays and overdubs; Play a third time stops overdubbing. "
                 "Then Alt off.",
                 juce::dontSendNotification);
    hint.setColour(juce::Label::textColourId, kDim);
    hint.setFont(juce::FontOptions(12.5f));
    hint.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(hint);

    list = std::make_unique<juce::GenericAudioProcessorEditor>(p);
    addAndMakeVisible(*list);

    const int width = kMargin * 2 + kRing + kGap + 5 * (kPadW + kGap) + kGap + kPadW;
    setSize(std::max(width, list->getWidth()), kTop + 400);
    refresh();
    startTimerHz(30);
}

ScotykachEditor::~ScotykachEditor()
{
    stopTimer();
    for (auto& pd : pads) pd->release();   // never leave a pad held
}

ScotykachEditor::Pad* ScotykachEditor::pad(const juce::String& id)
{
    for (size_t i = 0; i < padIds.size(); ++i)
        if (padIds[i] == id) return pads[i].get();
    return nullptr;
}

void ScotykachEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(20.f, juce::Font::bold));
    for (int d = 0; d < 2; ++d)
        if (rings[d]) g.drawText(d == 0 ? "A" : "B", rings[d]->getBounds(), juce::Justification::centred);
}

void ScotykachEditor::resized()
{
    for (int d = 0; d < 2; ++d)
    {
        const int y = kMargin + d * (kRing + 26);
        rings[d]->setBounds(kMargin, y, kRing, kRing);
        const int x0 = kMargin + kRing + kGap;
        for (int i = 0; i < 5; ++i)
            pads[(size_t)(d * 5 + i)]->setBounds(x0 + i * (kPadW + kGap), y + 6, kPadW, kPadH);
        status[d].setBounds(x0, y + 6 + kPadH + 4, 5 * (kPadW + kGap), 22);
    }
    const int xr = kMargin + kRing + kGap + 5 * (kPadW + kGap) + kGap;
    for (int i = 0; i < 3; ++i)
        pads[(size_t)(10 + i)]->setBounds(xr, kMargin + 6 + i * (kPadH + kGap + 20), kPadW, kPadH);
    hint.setBounds(kMargin, kTop - 46, getWidth() - 2 * kMargin, 44);
    list->setBounds(0, kTop, getWidth(), getHeight() - kTop);
}

juce::String ScotykachEditor::statusText(int deck) const
{
    const auto  st   = plugin.core().deckStatus(deck);
    const auto* mode = plugin.parameters.getRawParameterValue(deck == 0 ? "a_mode" : "b_mode");
    const bool  slice = mode != nullptr && (int)mode->load() == 1;
    const auto  len  = juce::String(st.seconds, 1) + " s";
    switch (st.state)
    {
        case DeckState::Empty:       return "Empty";
        case DeckState::Armed:       return slice ? "Armed: recording starts on the next beat"
                                                  : "Armed: recording starts at the next sound";
        case DeckState::Recording:   return "Recording (Alt + Play closes the loop)";
        case DeckState::Overdubbing: return "Playing " + len + ", overdubbing (Alt + Play stops it)";
        case DeckState::Playing:     return "Playing " + len;
        case DeckState::Stopped:     return "Stopped, " + len + " loop";
    }
    return {};
}

void ScotykachEditor::refresh()
{
    const auto& core = plugin.core();
    for (size_t i = 0; i < pads.size(); ++i)
        pads[i]->refresh(kPadSpecs[i].led >= 0 ? core.led(kPadSpecs[i].led) : 0);
    for (int d = 0; d < 2; ++d)
    {
        rings[d]->refresh(core);
        status[d].setText(statusText(d), juce::dontSendNotification);
    }
}
