/**
 * @file PluginEditor.h
 * @brief The plugin window: both decks' buttons and switches, lit as the
 *        module's LEDs are, above a list of every knob and setting.
 *
 *   - No Alt: each thing the module does with Alt held has its own button
 *     or switch. Record steps through recording (record, close the loop,
 *     overdub, stop overdubbing); Clear empties the deck; Seq Rec and Seq
 *     Clear arm and clear the sequencer; the locks, Quantise, Mod Sync and
 *     Free Size are switches.
 *   - Buttons press on mouse down and release on mouse up, as a finger
 *     does, so holding Grit or Flux plays the effect.
 *   - Each deck shows its LED ring and what it is doing.
 *
 * A stand-in until the panel-style window (milestone S4).
 */

#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class ScotykachEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
  public:
    explicit ScotykachEditor(ScotykachAudioProcessor&);
    ~ScotykachEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** A button for a button parameter: held while the mouse is down. */
    class Pad : public juce::Component
    {
      public:
        Pad(juce::RangedAudioParameter&, juce::String text);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { press(); }
        void mouseUp(const juce::MouseEvent&) override   { release(); }

        /** What the mouse does, without one (tests). */
        void press();
        void release();

        /** Shows a colour (0xRRGGBB, 0 for none) and, if given, new text. */
        void show(uint32_t colour, const juce::String& text = {});
        const juce::String& getText() const { return text; }

      private:
        juce::RangedAudioParameter& param;
        juce::String text;
        bool     down   = false;
        uint32_t colour = 0;
    };

    /** An Off/On switch for a two-way choice parameter. */
    class Switch : public juce::Component
    {
      public:
        Switch(juce::RangedAudioParameter&, juce::String text);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { toggle(); }

        void toggle();
        bool isOn() const { return param.getValue() >= .5f; }
        void refresh() { if (isOn() != shown) { shown = isOn(); repaint(); } }

      private:
        juce::RangedAudioParameter& param;
        juce::String text;
        bool shown = false;
    };

    /** The control for a parameter ID, for tests (nullptr if none). */
    Pad*    pad(const juce::String& paramId);
    Switch* toggle(const juce::String& paramId);

    /** Shows the module's current LEDs and deck state (the timer's job). */
    void refresh();

  private:
    class Ring;
    class ParamList;

    struct Deck
    {
        juce::Label status;
        juce::ComboBox mode;
        std::unique_ptr<juce::ComboBoxParameterAttachment> modeAttachment;
        std::unique_ptr<Ring> ring;
        Pad* record = nullptr;
        Pad* play   = nullptr;
        Pad* seqRec = nullptr;
        std::vector<Pad*>    grid;       // in layout order
        std::vector<Switch*> switches;
        std::vector<int>     leds;       // per grid pad: Hardware::LedId, or -1
    };

    void timerCallback() override { refresh(); }
    Pad*    addPad(Deck&, const char* id, const char* text, int led);
    Switch* addSwitch(const char* id, const char* text);
    juce::String statusText(int deck) const;

    ScotykachAudioProcessor& plugin;

    std::vector<std::unique_ptr<Pad>>    pads;
    std::vector<juce::String>            padIds;
    std::vector<std::unique_ptr<Switch>> switches;
    std::vector<juce::String>            switchIds;
    Deck decks[2];
    Pad*    tap = nullptr;
    Switch* overdub = nullptr;
    juce::Label hint;

    std::unique_ptr<ParamList> list;
    juce::Viewport             listView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScotykachEditor)
};
