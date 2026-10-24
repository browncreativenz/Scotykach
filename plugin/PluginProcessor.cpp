/**
 * @file PluginProcessor.cpp
 * @brief See PluginProcessor.h.
 */

#include "PluginProcessor.h"

#include <cmath>
#include <limits>

#include "PluginEditor.h"
#include "ScotykachModule.h"

namespace params = scotykach::params;
using scotykach::Panel;
using spotykach::Core;
using spotykach::Deck;

namespace {

/** State: a magic word and version, the parameters as XML, then each
 *  deck's loop as a frame count and interleaved 32-bit floats. */
constexpr int kStateMagic   = 0x31594b53;   // "SKY1"
constexpr int kStateVersion = 1;

juce::String TempoText(float v, int)
{
    return juce::String(20.0f + v * 230.0f, 1) + " BPM";   // the firmware's Tempo range
}

} // namespace

ScotykachAudioProcessor::ScotykachAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Scotykach", CreateLayout())
{
    for (int p = 0; p < params::kCount; ++p)
    {
        const auto& info = params::Get(p);
        raw[(size_t)p]        = parameters.getRawParameterValue(info.id);
        hostParams[(size_t)p] = parameters.getParameter(info.id);
        pushed[(size_t)p]     = info.defaultValue;
        jassert(raw[(size_t)p] != nullptr && hostParams[(size_t)p] != nullptr);
        jassert(hostParams[(size_t)p]->getParameterIndex() == p);
        if (info.kind == params::Kind::Button) hostParams[(size_t)p]->addListener(this);
    }
    startTimerHz(30);
}

ScotykachAudioProcessor::~ScotykachAudioProcessor()
{
    stopTimer();
    for (int p = 0; p < params::kCount; ++p)
        if (params::Get(p).kind == params::Kind::Button) hostParams[(size_t)p]->removeListener(this);
}

void ScotykachAudioProcessor::parameterValueChanged(int index, float value)
{
    if (index >= 0 && index < params::kCount) processor.setParameter(index, value >= .5f ? 1.f : 0.f);
}

juce::AudioProcessorValueTreeState::ParameterLayout ScotykachAudioProcessor::CreateLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const int tempo = params::GlobalControl((int)Panel::Global::Tempo);

    for (int p = 0; p < params::kCount; ++p)
    {
        const auto&           info = params::Get(p);
        const juce::ParameterID id { info.id, 1 };
        switch (info.kind)
        {
            case params::Kind::Unit:
            {
                auto attributes = juce::AudioParameterFloatAttributes();
                if (p == tempo) attributes = attributes.withStringFromValueFunction(TempoText);
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    id, info.name, juce::NormalisableRange<float>(0.f, 1.f), info.defaultValue, attributes));
                break;
            }
            case params::Kind::Cv:
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    id, info.name, juce::NormalisableRange<float>(-1.f, 1.f), info.defaultValue));
                break;
            case params::Kind::Choice:
            {
                juce::StringArray choices;
                for (int c = 0; c < info.numChoices; ++c) choices.add(info.choices[c]);
                layout.add(std::make_unique<juce::AudioParameterChoice>(
                    id, info.name, choices, (int)info.defaultValue));
                break;
            }
            case params::Kind::Button:
                layout.add(std::make_unique<juce::AudioParameterBool>(id, info.name, false));
                break;
        }
    }
    return layout;
}

bool ScotykachAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

double ScotykachAudioProcessor::getTailLengthSeconds() const
{
    // VST3 reports infinity as an infinite tail; the AU passes the number
    // straight on, so it gets an hour.
    if (wrapperType == wrapperType_AudioUnit) return 3600.0;
    return std::numeric_limits<double>::infinity();
}

void ScotykachAudioProcessor::prepareToPlay(double sampleRate, int)
{
    processor.prepare(sampleRate);
    setLatencySamples(processor.latencySamples());
    midi.reserve(kMaxMidiPerBlock);

    bypassDelay.setSize(2, juce::jmax(1, processor.latencySamples()));
    bypassDelay.clear();
    bypassPos = 0;
}

void ScotykachAudioProcessor::reset()
{
    // The loops are the music: a host reset (a transport jump, a stop) must
    // not wipe them. Only the bypass delay starts over.
    bypassDelay.clear();
    bypassPos = 0;
}

void ScotykachAudioProcessor::PushParameters()
{
    for (int p = 0; p < params::kCount; ++p)
    {
        const float v = raw[(size_t)p]->load(std::memory_order_relaxed);
        if (juce::exactlyEqual(v, pushed[(size_t)p])) continue;
        processor.setParameter(p, v);
        pushed[(size_t)p] = v;
    }
}

void ScotykachAudioProcessor::syncParametersToHost()
{
    for (int p = 0; p < params::kCount; ++p)
    {
        if (!processor.takeChanged(p)) continue;
        auto* param = hostParams[(size_t)p];
        param->setValueNotifyingHost(param->convertTo0to1(processor.getParameter(p)));
    }
}

scotykach::Processor::Transport ScotykachAudioProcessor::ReadTransport()
{
    scotykach::Processor::Transport t;
    auto* head = getPlayHead();
    if (head == nullptr) return t;
    const auto pos = head->getPosition();
    if (!pos.hasValue()) return t;
    const auto ppq = pos->getPpqPosition();
    if (!ppq.hasValue()) return t;   // no beat position: nothing to follow

    t.valid   = true;
    t.playing = pos->getIsPlaying();
    t.bpm     = pos->getBpm().orFallback(120.0);
    t.ppq     = *ppq;
    const auto sig = pos->getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature {});
    t.beatsPerBar  = sig.denominator > 0 ? sig.numerator * 4.0 / sig.denominator : 4.0;
    t.barStartPpq  = pos->getPpqPositionOfLastBarStart()
                         .orFallback(std::floor(t.ppq / t.beatsPerBar) * t.beatsPerBar);
    return t;
}

void ScotykachAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getWritePointer(1);
    if (getTotalNumInputChannels() == 1) std::copy(l, l + n, r);   // mono in feeds both channels

    PushParameters();

    midi.clear();
    for (const auto m : midiMessages)
    {
        if (m.numBytes < 1 || m.numBytes > 3 || midi.size() == kMaxMidiPerBlock) continue;
        scotykach::Processor::MidiMessage msg { juce::jlimit(0, n - 1, m.samplePosition), { 0, 0, 0 } };
        for (int i = 0; i < m.numBytes; ++i) msg.data[i] = m.data[i];
        midi.push_back(msg);
    }
    midiMessages.clear();

    processor.process(l, r, l, r, n, ReadTransport(), midi.data(), (int)midi.size());
}

void ScotykachAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    midiMessages.clear();
    const int n   = buffer.getNumSamples();
    const int lat = getLatencySamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom(1, 0, buffer, 0, 0, n);
    if (lat <= 0) return;

    float* io[2]   = { buffer.getWritePointer(0), buffer.getWritePointer(1) };
    float* ring[2] = { bypassDelay.getWritePointer(0), bypassDelay.getWritePointer(1) };
    for (int i = 0; i < n; i++)
    {
        for (int c = 0; c < 2; c++)
        {
            const float x = io[c][i];
            io[c][i] = ring[c][bypassPos];
            ring[c][bypassPos] = x;
        }
        bypassPos = (bypassPos + 1) % lat;
    }
}

juce::AudioProcessorEditor* ScotykachAudioProcessor::createEditor()
{
    return new ScotykachEditor(*this);
}

void ScotykachAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state = parameters.copyState();
    state.setProperty("version", JucePlugin_VersionString, nullptr);

    juce::MemoryOutputStream out(destData, false);
    out.writeInt(kStateMagic);
    out.writeInt(kStateVersion);
    if (auto xml = state.createXml()) out.writeString(xml->toString());
    else                              out.writeString({});

    // The loops are read while the audio thread may still be overdubbing
    // them; a save mid-overdub can catch a block of the pass in progress.
    // Stopping the audio for the copy would be heard, so it isn't.
    std::vector<float> loop;
    out.writeInt(2);
    for (const auto ref : { Deck::A, Deck::B })
    {
        loop.resize(2 * Panel::maxLoopFrames());
        const auto frames = processor.module().control([&](Panel& p, Core&) {
            return p.readLoop(ref, loop.data(), Panel::maxLoopFrames());
        });
        out.writeInt64((juce::int64)frames);
        out.write(loop.data(), frames * 2 * sizeof(float));
    }
}

void ScotykachAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in(data, (size_t)sizeInBytes, false);
    if (sizeInBytes < 8 || in.readInt() != kStateMagic || in.readInt() > kStateVersion) return;

    if (auto xml = juce::parseXML(in.readString()))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));

    const int decks = in.readInt();
    std::vector<float> loops[2];
    for (int d = 0; d < decks && d < 2; ++d)
    {
        const auto frames = (size_t)juce::jlimit<juce::int64>(0, (juce::int64)Panel::maxLoopFrames(), in.readInt64());
        loops[d].resize(frames * 2);
        if (frames > 0 && in.read(loops[d].data(), (int)(frames * 2 * sizeof(float))) != (int)(frames * 2 * sizeof(float)))
            loops[d].clear();   // a truncated state: leave that deck empty
    }

    // The decks change while no block runs.
    suspendProcessing(true);
    processor.module().control([&](Panel& p, Core&) {
        p.writeLoop(Deck::A, loops[0].data(), loops[0].size() / 2);
        p.writeLoop(Deck::B, loops[1].data(), loops[1].size() / 2);
    });
    suspendProcessing(false);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScotykachAudioProcessor();
}
