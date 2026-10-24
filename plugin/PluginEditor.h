/**
 * @file PluginEditor.h
 * @brief The plugin window: the module's pads, lit as its LEDs are, above
 *        JUCE's generic list of every parameter.
 *
 *   - Pads press on mouse down and release on mouse up, as a finger does,
 *     so the module's gestures work: Alt + Play records, holding Grit or
 *     Flux plays the effect, holding Seq clears the sequence.
 *   - Alt latches (one click on, one off), since the mouse can't hold Alt
 *     and click Play at once. It lights while on.
 *   - Each pad shows its LED on the module, and each deck its LED ring and
 *     what it is doing (armed, recording, overdubbing, ...).
 *
 * A stand-in until the panel-style window (milestone S4).
 */

#pragma once

#include <array>
#include <memory>

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

    /** One pad; public for tests. */
    class Pad : public juce::Component
    {
      public:
        Pad(juce::RangedAudioParameter&, juce::String text, int led, bool latching);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

        /** What the mouse does, without one. */
        void press();
        void release();

        bool isOn() const { return param.getValue() >= .5f; }
        void refresh(uint32_t ledColour);

      private:
        juce::RangedAudioParameter& param;
        juce::String text;
        int      led;
        bool     latching;
        bool     down   = false;
        uint32_t colour = 0;
        bool     shownOn = false;
    };

    /** The pad for a button parameter, for tests. */
    Pad* pad(const juce::String& paramId);

    /** Shows the module's current LEDs and deck state (the timer's job). */
    void refresh();

  private:
    class Ring;

    void timerCallback() override { refresh(); }
    juce::String statusText(int deck) const;

    ScotykachAudioProcessor& plugin;

    std::vector<std::unique_ptr<Pad>> pads;
    std::vector<juce::String>         padIds;
    std::unique_ptr<Ring>             rings[2];
    juce::Label                       status[2];
    juce::Label                       hint;
    std::unique_ptr<juce::GenericAudioProcessorEditor> list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScotykachEditor)
};
