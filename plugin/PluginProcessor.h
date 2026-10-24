/**
 * @file PluginProcessor.h
 * @brief The JUCE plugin: host parameters, transport, MIDI and state around
 *        scotykach::Processor.
 *
 *   - Parameters come from the table (core/ScotykachParams.h). Each block,
 *     the ones the host changed go to the processor. Values the firmware
 *     moves itself (tap tempo, a MIDI CC, Size fitting the tempo) go back
 *     to the host from a message-thread timer.
 *   - The host's playhead becomes the processor's transport, which the
 *     module's clock follows (the Clock parameter).
 *   - MIDI in reaches the module at each message's own sample.
 *   - State is the parameters and both decks' recorded loops, at full
 *     quality (32-bit float), so a saved set reopens with its loops.
 *   - Bypass delays the dry signal by the plugin's latency, so toggling it
 *     doesn't shift the audio in time.
 */

#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ScotykachParams.h"
#include "ScotykachProcessor.h"

class ScotykachAudioProcessor : public juce::AudioProcessor,
                                private juce::Timer,
                                private juce::AudioProcessorParameter::Listener
{
  public:
    ScotykachAudioProcessor();
    ~ScotykachAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi() const override  { return true; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }

    /** A loop plays on with no input at all; an infinite tail (an hour for
     *  the AU) keeps hosts from putting the plugin to sleep when its track
     *  goes quiet. */
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override                             { return 1; }
    int  getCurrentProgram() override                          { return 0; }
    void setCurrentProgram(int) override                       {}
    const juce::String getProgramName(int) override            { return {}; }
    void changeProgramName(int, const juce::String&) override  {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    /** Sends values the firmware changed to the host. The timer calls it on
     *  the message thread; tests call it directly. */
    void syncParametersToHost();

    /** The processor, for tests and the plugin window. */
    scotykach::Processor& core() { return processor; }

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout CreateLayout();

    void timerCallback() override { syncParametersToHost(); }

    /* Buttons go to the processor as they change, not once a block, so a
     * click released before the next block still presses. */
    void parameterValueChanged(int index, float value) override;
    void parameterGestureChanged(int, bool) override {}

    /** Passes host parameter changes to the processor. */
    void PushParameters();
    scotykach::Processor::Transport ReadTransport();

    scotykach::Processor processor;

    std::array<std::atomic<float>*, scotykach::params::kCount>  raw {};
    std::array<juce::RangedAudioParameter*, scotykach::params::kCount> hostParams {};
    std::array<float, scotykach::params::kCount> pushed {};

    std::vector<scotykach::Processor::MidiMessage> midi;   // reserved in prepareToPlay
    static constexpr size_t kMaxMidiPerBlock = 4096;

    /* Bypass keeps the dry signal on the same latency as processing. */
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScotykachAudioProcessor)
};
