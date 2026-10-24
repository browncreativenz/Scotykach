/**
 * @file PluginEditor.cpp
 * @brief See PluginEditor.h.
 */

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include "hw/hardware.h"

using HW        = spotykach::Hardware;
using DeckState = scotykach::Processor::DeckState;
namespace params = scotykach::params;

namespace {

constexpr int kPadW = 70, kPadH = 40, kGap = 6, kRing = 84, kMargin = 14;
constexpr int kSwitchH = 26;
constexpr int kGridW   = 4 * kPadW + 3 * kGap;
constexpr int kDeckW   = kRing + 10 + kGridW;
constexpr int kWidth   = 2 * kMargin + 2 * kDeckW + 3 * kMargin;
constexpr int kDeckH   = 30 + 3 * (kPadH + kGap) + 2 * (kSwitchH + kGap);
constexpr int kTop     = kMargin + kDeckH + 8 + kPadH + kMargin;   // the controls, above the list
constexpr int kListH   = 360;

const juce::Colour kBackground { 0xff1b1b1e };
const juce::Colour kPanel      { 0xff232328 };
const juce::Colour kPadOff     { 0xff2e2e33 };
const juce::Colour kText       { 0xffe8e8ea };
const juce::Colour kDim        { 0xff8a8a92 };

constexpr uint32_t kRecordRed  = 0xe0302a;
constexpr uint32_t kOverdubRed = 0xe06a2a;
constexpr uint32_t kSeqArmed   = 0x9a5ce0;
constexpr uint32_t kSwitchOn   = 0x3d8fd6;

/** An LED colour (brightness already applied) as a fill over a dark pad. */
juce::Colour LedFill(uint32_t rgb)
{
    const int r = (int)(rgb >> 16) & 0xFF, g = (int)(rgb >> 8) & 0xFF, b = (int)rgb & 0xFF;
    const int m = std::max({ r, g, b });
    if (m == 0) return juce::Colours::transparentBlack;
    return juce::Colour((uint8_t)(r * 255 / m), (uint8_t)(g * 255 / m), (uint8_t)(b * 255 / m))
        .withAlpha((float)m / 255.f);
}

/** Choices shown as switches or menus above the list, not in it. */
bool ShownAbove(const juce::String& id)
{
    static const juce::StringArray ids {
        "a_mode", "b_mode", "after_rec",
        "a_rec_source", "b_rec_source", "a_grit_lock", "b_grit_lock", "a_flux_lock", "b_flux_lock",
        "a_quantise", "b_quantise", "a_mod_sync", "b_mod_sync", "a_free_size", "b_free_size",
    };
    return ids.contains(id);
}

} // namespace

// ── Pad ─────────────────────────────────────────────────────────────────────

ScotykachEditor::Pad::Pad(juce::RangedAudioParameter& p, juce::String t)
    : param(p), text(std::move(t))
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ScotykachEditor::Pad::press()
{
    if (down) return;
    down = true;
    param.beginChangeGesture();
    param.setValueNotifyingHost(1.f);
    repaint();
}

void ScotykachEditor::Pad::release()
{
    if (!down) return;
    down = false;
    param.setValueNotifyingHost(0.f);
    param.endChangeGesture();
    repaint();
}

void ScotykachEditor::Pad::show(uint32_t c, const juce::String& t)
{
    const bool newText = t.isNotEmpty() && t != text;
    if (c == colour && !newText) return;
    colour = c;
    if (newText) text = t;
    repaint();
}

void ScotykachEditor::Pad::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.5f);
    const auto fill = LedFill(colour);
    g.setColour(kPadOff);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(down ? kText : kText.withAlpha(.18f));
    g.drawRoundedRectangle(r, 7.f, down ? 2.5f : 1.f);

    g.setColour(fill.getPerceivedBrightness() * fill.getFloatAlpha() > .55f ? kBackground : kText);
    g.setFont(juce::FontOptions(14.f, juce::Font::bold));
    g.drawFittedText(text, getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

// ── Switch ──────────────────────────────────────────────────────────────────

ScotykachEditor::Switch::Switch(juce::RangedAudioParameter& p, juce::String t)
    : param(p), text(std::move(t))
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    shown = isOn();
}

void ScotykachEditor::Switch::toggle()
{
    param.beginChangeGesture();
    param.setValueNotifyingHost(isOn() ? 0.f : 1.f);
    param.endChangeGesture();
    refresh();
}

void ScotykachEditor::Switch::paint(juce::Graphics& g)
{
    const auto r  = getLocalBounds().toFloat().reduced(1.f);
    const bool on = isOn();
    g.setColour(on ? juce::Colour(0xff000000 | kSwitchOn) : kPadOff);
    g.fillRoundedRectangle(r, r.getHeight() * .5f);
    g.setColour(on ? kText : kText.withAlpha(.18f));
    g.drawRoundedRectangle(r, r.getHeight() * .5f, 1.f);
    g.setColour(on ? kText : kDim);
    g.setFont(juce::FontOptions(12.5f));
    g.drawFittedText(text, getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
}

// ── Ring ────────────────────────────────────────────────────────────────────

/** A deck's 32-LED ring, clockwise from the top. */
class ScotykachEditor::Ring : public juce::Component
{
  public:
    Ring(int first, juce::String name) : firstLed(first), label(std::move(name))
    {
        setInterceptsMouseClicks(false, false);
    }

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
        g.setColour(kDim);
        g.setFont(juce::FontOptions(22.f, juce::Font::bold));
        g.drawText(label, getLocalBounds(), juce::Justification::centred);
    }

  private:
    int firstLed;
    juce::String label;
    std::array<uint32_t, HW::kNumLedsPerRing> colours {};
};

// ── Parameter list ──────────────────────────────────────────────────────────

/** Every knob and the remaining settings, Deck A and B side by side, then
 *  the global ones and the CV inputs. */
class ScotykachEditor::ParamList : public juce::Component
{
  public:
    explicit ParamList(ScotykachAudioProcessor& p)
    {
        const char* titles[4] = { "Deck A", "Deck B", "Global", "CV inputs" };
        for (int c = 0; c < 4; ++c) columns[c].title = titles[c];

        for (int i = 0; i < params::kCount; ++i)
        {
            const auto& info = params::Get(i);
            if (info.kind == params::Kind::Button || ShownAbove(info.id)) continue;
            const juce::String id = info.id;
            const int col = id.startsWith("a_") ? 0 : id.startsWith("b_") ? 1 : id.startsWith("cv_") ? 3 : 2;
            auto* param = p.parameters.getParameter(id);

            auto row = std::make_unique<Row>();
            row->label.setText(col < 2 ? juce::String(info.name).substring(2) : juce::String(info.name),
                               juce::dontSendNotification);
            row->label.setColour(juce::Label::textColourId, kText);
            row->label.setFont(juce::FontOptions(13.f));
            row->label.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(row->label);

            if (info.kind == params::Kind::Choice)
            {
                row->combo = std::make_unique<juce::ComboBox>();
                row->combo->addItemList(param->getAllValueStrings(), 1);
                row->comboAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*param, *row->combo);
                addAndMakeVisible(*row->combo);
            }
            else
            {
                row->slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                row->slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 20);
                row->sliderAttachment = std::make_unique<juce::SliderParameterAttachment>(*param, *row->slider);
                addAndMakeVisible(*row->slider);
            }
            columns[col].rows.push_back(std::move(row));
        }
    }

    int heightFor() const
    {
        auto h = [&](int c) { return kHeader + (int)columns[c].rows.size() * kRowH; };
        return std::max(h(0), h(1)) + std::max(h(2), h(3)) + 3 * kGapY;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanel);
        g.setColour(kDim);
        g.setFont(juce::FontOptions(14.f, juce::Font::bold));
        for (const auto& c : columns) g.drawText(c.title, c.titleArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int colW = (getWidth() - 3 * kMargin) / 2;
        int y = kGapY;
        for (int pair = 0; pair < 2; ++pair)
        {
            int bottom = y;
            for (int side = 0; side < 2; ++side)
            {
                auto& c = columns[pair * 2 + side];
                const int x = kMargin + side * (colW + kMargin);
                c.titleArea = { x, y, colW, kHeader };
                int ry = y + kHeader;
                for (auto& row : c.rows)
                {
                    row->label.setBounds(x, ry, 130, kRowH);
                    const juce::Rectangle<int> ctl { x + 136, ry + 2, colW - 136, kRowH - 4 };
                    if (row->slider) row->slider->setBounds(ctl);
                    if (row->combo)  row->combo->setBounds(ctl.withWidth(std::min(180, ctl.getWidth())));
                    ry += kRowH;
                }
                bottom = std::max(bottom, ry);
            }
            y = bottom + kGapY;
        }
    }

  private:
    static constexpr int kRowH = 26, kHeader = 28, kGapY = 8;

    struct Row
    {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttachment;
    };
    struct Column
    {
        juce::String title;
        juce::Rectangle<int> titleArea;
        std::vector<std::unique_ptr<Row>> rows;
    };
    Column columns[4];
};

// ── Editor ──────────────────────────────────────────────────────────────────

ScotykachEditor::Pad* ScotykachEditor::addPad(Deck& d, const char* id, const char* text, int led)
{
    auto* param = plugin.parameters.getParameter(id);
    jassert(param != nullptr);
    pads.push_back(std::make_unique<Pad>(*param, text));
    padIds.push_back(id);
    addAndMakeVisible(*pads.back());
    d.grid.push_back(pads.back().get());
    d.leds.push_back(led);
    return pads.back().get();
}

ScotykachEditor::Switch* ScotykachEditor::addSwitch(const char* id, const char* text)
{
    auto* param = plugin.parameters.getParameter(id);
    jassert(param != nullptr);
    switches.push_back(std::make_unique<Switch>(*param, text));
    switchIds.push_back(id);
    addAndMakeVisible(*switches.back());
    return switches.back().get();
}

ScotykachEditor::ScotykachEditor(ScotykachAudioProcessor& p)
    : AudioProcessorEditor(p), plugin(p)
{
    for (int i = 0; i < 2; ++i)
    {
        auto& d = decks[i];
        const bool a = i == 0;
        auto id = [&](const char* name) { return (juce::String(a ? "a_" : "b_") + name).toStdString(); };

        d.ring = std::make_unique<Ring>(a ? HW::LED_RING_A : HW::LED_RING_B, a ? "A" : "B");
        addAndMakeVisible(*d.ring);

        auto* mode = p.parameters.getParameter(id("mode"));
        d.mode.addItemList(mode->getAllValueStrings(), 1);
        d.modeAttachment = std::make_unique<juce::ComboBoxParameterAttachment>(*mode, d.mode);
        addAndMakeVisible(d.mode);

        d.status.setColour(juce::Label::textColourId, kText);
        d.status.setFont(juce::FontOptions(13.5f));
        addAndMakeVisible(d.status);

        // Layout order: Record (two wide), Play, Rev / Clear, Grit, Flux, Seq / Seq Rec, Seq Clear.
        d.record = addPad(d, id("record").c_str(), "Record", -1);
        d.play   = addPad(d, id("play").c_str(), "Play", a ? HW::LED_PLAY_A : HW::LED_PLAY_B);
        addPad(d, id("rev").c_str(), "Rev", a ? HW::LED_REV_A : HW::LED_REV_B);
        addPad(d, id("clear").c_str(), "Clear", -1);
        addPad(d, id("grit").c_str(), "Grit", a ? HW::LED_GRIT_A : HW::LED_GRIT_B);
        addPad(d, id("flux").c_str(), "Flux", a ? HW::LED_FLUX_A : HW::LED_FLUX_B);
        addPad(d, id("seq").c_str(), "Seq", -1);
        d.seqRec = addPad(d, id("seq_rec").c_str(), "Seq Rec", -1);
        addPad(d, id("seq_clear").c_str(), "Seq Clear", -1);

        d.switches = {
            addSwitch(id("rec_source").c_str(), "Record the deck"),
            addSwitch(id("grit_lock").c_str(),  "Grit lock"),
            addSwitch(id("flux_lock").c_str(),  "Flux lock"),
            addSwitch(id("quantise").c_str(),   "Quantise pitch"),
            addSwitch(id("mod_sync").c_str(),   "Sync mod"),
            addSwitch(id("free_size").c_str(),  "Free size"),
        };
    }

    Deck none;   // Tap belongs to neither deck
    tap = addPad(none, "tap", "Tap", HW::LED_CLOCK_IN);
    overdub = addSwitch("after_rec", "Overdub after recording");

    hint.setText("Buttons act while held. Record steps through: Record, Close loop, Overdub, Stop overdub. "
                 "Hold Grit or Flux to play the effect.",
                 juce::dontSendNotification);
    hint.setColour(juce::Label::textColourId, kDim);
    hint.setFont(juce::FontOptions(12.5f));
    hint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hint);

    list = std::make_unique<ParamList>(p);
    listView.setViewedComponent(list.get(), false);
    listView.setScrollBarsShown(true, false);
    addAndMakeVisible(listView);

    setSize(kWidth, kTop + kListH);
    refresh();
    startTimerHz(30);
}

ScotykachEditor::~ScotykachEditor()
{
    stopTimer();
    for (auto& pd : pads) pd->release();   // never leave a button held
}

ScotykachEditor::Pad* ScotykachEditor::pad(const juce::String& id)
{
    for (size_t i = 0; i < padIds.size(); ++i)
        if (padIds[i] == id) return pads[i].get();
    return nullptr;
}

ScotykachEditor::Switch* ScotykachEditor::toggle(const juce::String& id)
{
    for (size_t i = 0; i < switchIds.size(); ++i)
        if (switchIds[i] == id) return switches[i].get();
    return nullptr;
}

void ScotykachEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    g.setColour(kPanel);
    for (int i = 0; i < 2; ++i)
    {
        const int x = kMargin + i * (kDeckW + 3 * kMargin);
        g.fillRoundedRectangle(juce::Rectangle<int>(x - 8, kMargin - 6, kDeckW + 16, kDeckH + 12).toFloat(), 10.f);
    }
}

void ScotykachEditor::resized()
{
    for (int i = 0; i < 2; ++i)
    {
        auto& d = decks[i];
        const int x = kMargin + i * (kDeckW + 3 * kMargin), y = kMargin;
        d.mode.setBounds(x, y, 96, 24);
        d.status.setBounds(x + 104, y, kDeckW - 104, 24);
        d.ring->setBounds(x, y + 30, kRing, kRing);

        const int gx = x + kRing + 10, gy = y + 30;
        auto cell = [&](int col, int row, int span = 1) {
            return juce::Rectangle<int>(gx + col * (kPadW + kGap), gy + row * (kPadH + kGap),
                                        span * kPadW + (span - 1) * kGap, kPadH);
        };
        const juce::Rectangle<int> cells[] = {
            cell(0, 0, 2), cell(2, 0), cell(3, 0),
            cell(0, 1), cell(1, 1), cell(2, 1), cell(3, 1),
            cell(0, 2), cell(1, 2),
        };
        for (size_t c = 0; c < d.grid.size(); ++c) d.grid[c]->setBounds(cells[c]);

        const int sy = gy + 3 * (kPadH + kGap) + 2;
        const int sw = (kDeckW - 2 * kGap) / 3;
        for (size_t s = 0; s < d.switches.size(); ++s)
            d.switches[s]->setBounds(x + (int)(s % 3) * (sw + kGap), sy + (int)(s / 3) * (kSwitchH + kGap), sw, kSwitchH);
    }

    const int y = kMargin + kDeckH + 8;
    tap->setBounds(kMargin, y, kPadW, kPadH);
    overdub->setBounds(kMargin + kPadW + 12, y + (kPadH - kSwitchH) / 2, 190, kSwitchH);
    hint.setBounds(kMargin + kPadW + 214, y, getWidth() - (kMargin + kPadW + 214) - kMargin, kPadH);

    listView.setBounds(0, kTop, getWidth(), getHeight() - kTop);
    list->setSize(getWidth() - listView.getScrollBarThickness(), list->heightFor());
}

juce::String ScotykachEditor::statusText(int deck) const
{
    const auto  st    = plugin.core().deckStatus(deck);
    const auto* mode  = plugin.parameters.getRawParameterValue(deck == 0 ? "a_mode" : "b_mode");
    const bool  slice = mode != nullptr && (int)mode->load() == 1;
    const auto  len   = juce::String(st.seconds, 1) + " s";
    juce::String s;
    switch (st.state)
    {
        case DeckState::Empty:       s = "Empty"; break;
        case DeckState::Armed:       s = slice ? "Waiting for the next beat" : "Waiting for sound"; break;
        case DeckState::Recording:   s = "Recording"; break;
        case DeckState::Overdubbing: s = "Overdubbing, " + len + " loop"; break;
        case DeckState::Playing:     s = "Playing, " + len + " loop"; break;
        case DeckState::Stopped:     s = "Stopped, " + len + " loop"; break;
    }
    if (st.seqArmed) s << ", sequencer recording";
    return s;
}

void ScotykachEditor::refresh()
{
    auto& core = plugin.core();
    const bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

    for (int i = 0; i < 2; ++i)
    {
        auto& d = decks[i];
        const auto st = core.deckStatus(i);
        for (size_t c = 0; c < d.grid.size(); ++c)
            if (d.grid[c] != d.record) d.grid[c]->show(d.leds[c] >= 0 ? core.led(d.leds[c]) : 0);

        switch (st.state)
        {
            case DeckState::Empty:       d.record->show(0, "Record"); break;
            case DeckState::Stopped:     d.record->show(0, "Record new"); break;
            case DeckState::Armed:       d.record->show(blink ? kRecordRed : 0, "Cancel"); break;
            case DeckState::Recording:   d.record->show(kRecordRed, "Close loop"); break;
            case DeckState::Playing:     d.record->show(0, "Overdub"); break;
            case DeckState::Overdubbing: d.record->show(kOverdubRed, "Stop overdub"); break;
        }
        d.seqRec->show(st.seqArmed ? kSeqArmed : 0);
        d.status.setText(statusText(i), juce::dontSendNotification);
        d.ring->refresh(core);
    }
    tap->show(core.led(HW::LED_CLOCK_IN));
    for (auto& s : switches) s->refresh();
}
