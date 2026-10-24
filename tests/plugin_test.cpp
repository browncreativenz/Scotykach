/**
 * @file plugin_test.cpp
 * @brief The JUCE plugin driven as a host drives it: its parameters, the
 *        host's playhead, MIDI, values sent back to the host, saved state
 *        with the loops, mono input, and bypass.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

/* The checks compare floats exactly on purpose; JUCE's warning flags would
 * flag every one. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include <juce_events/juce_events.h>

#include "Check.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ScotykachModule.h"
#include "TestSignal.h"

using namespace scotykach;
namespace prm = scotykach::params;
using spotykach::Core;
using spotykach::Deck;

namespace {

constexpr int    kRate  = 44100;
constexpr int    kBlock = 441;   // 10 ms
constexpr double kFs    = 48000.0;

/** A host transport playing from beat 0 at a fixed tempo, in 4/4. */
class PlayHead : public juce::AudioPlayHead
{
  public:
    double bpm     = 130.0;
    bool   playing = true;
    int64_t sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        const double ppq = playing ? (double)sample / kRate * bpm / 60.0 : 0.0;
        info.setIsPlaying(playing);
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setTimeSignature(TimeSignature { 4, 4 });
        info.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
        return info;
    }
};

void SetParam(ScotykachAudioProcessor& plugin, const char* id, float plain)
{
    auto* p = plugin.parameters.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}

float GetParam(ScotykachAudioProcessor& plugin, const char* id)
{
    return plugin.parameters.getRawParameterValue(id)->load();
}

/** Runs `seconds` of the test signal (muted from `muteFrom`), with an
 *  action before each block. */
template <typename Action>
std::vector<float> Run(ScotykachAudioProcessor& plugin, PlayHead& head, double seconds, double muteFrom, Action&& act)
{
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer         midi;
    TestSignal               sig((float)kRate);
    std::vector<float>       out;
    const int blocks = (int)(seconds * kRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        const double t = b * kBlock / (double)kRate;
        midi.clear();
        act(t, midi);
        for (int i = 0; i < kBlock; ++i)
        {
            float l, r;
            sig.Next(l, r);
            if (t >= muteFrom) l = r = 0.f;
            buf.setSample(0, i, l);
            buf.setSample(1, i, r);
        }
        plugin.processBlock(buf, midi);
        head.sample += kBlock;
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
    }
    return out;
}

/** Alt held, tap Play: the module's record gesture, over three blocks. */
void RecordStep(ScotykachAudioProcessor& plugin, double t, double at)
{
    const double step = kBlock / (double)kRate;
    if (t >= at && t < at + step)                 SetParam(plugin, "alt", 1);
    if (t >= at + step && t < at + 2 * step)      SetParam(plugin, "a_play", 1);
    if (t >= at + 2 * step && t < at + 3 * step) { SetParam(plugin, "a_play", 0); SetParam(plugin, "alt", 0); }
}

struct Deckstate { std::size_t frames; bool playing; float tempo; };

Deckstate StateOf(ScotykachAudioProcessor& plugin)
{
    return plugin.core().module().control([](Panel& p, Core& c) {
        return Deckstate { p.loopFrames(Deck::A), c.deck(Deck::A).is_playing(), c.driver().tempo() };
    });
}

double Rms(const std::vector<float>& x, double from, double to)
{
    double s = 0;
    const auto a = (std::size_t)(from * kRate), b = std::min(x.size(), (std::size_t)(to * kRate));
    for (auto i = a; i < b; ++i) s += x[i] * x[i];
    return b > a ? std::sqrt(s / (double)(b - a)) : 0.0;
}

void TestParameters()
{
    ScotykachAudioProcessor plugin;
    CHECK((int)plugin.getParameters().size() == prm::kCount, "%d parameters", (int)plugin.getParameters().size());
    for (int p = 0; p < prm::kCount; ++p)
    {
        const auto& info = prm::Get(p);
        CHECK(GetParam(plugin, info.id) == info.defaultValue, "%s starts at %.4f", info.id, GetParam(plugin, info.id));
    }
    auto* tempo = plugin.parameters.getParameter("tempo");
    std::printf("  tempo shows \"%s\"\n", tempo->getCurrentValueAsText().toRawUTF8());
    CHECK(tempo->getCurrentValueAsText() == "120.0 BPM", "tempo text %s", tempo->getCurrentValueAsText().toRawUTF8());
}

/** Record with the pads, the clock on the host's tempo, a loop playing. */
void TestRecordAndClock(ScotykachAudioProcessor& plugin, PlayHead& head)
{
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(kRate, kBlock);
    SetParam(plugin, "a_mix", 1);
    SetParam(plugin, "crossfade", 0);
    const auto out = Run(plugin, head, 6.0, 2.5, [&](double t, juce::MidiBuffer&) {
        RecordStep(plugin, t, 0.5);
        RecordStep(plugin, t, 2.5);
    });
    const auto st = StateOf(plugin);
    std::printf("  latency %d, loop %.3f s, playing %d, module tempo %.2f (host %.0f), rms %.3f\n",
                plugin.getLatencySamples(), (double)st.frames / kFs, st.playing, st.tempo, head.bpm, Rms(out, 3.5, 6.0));
    CHECK(plugin.getLatencySamples() == plugin.core().latencySamples(), "latency reported");
    CHECK(st.playing && (double)st.frames > 1.8 * kFs && (double)st.frames <= 2.1 * kFs, "deck A should loop about 2 s");
    CHECK(std::abs(st.tempo - head.bpm) < 0.5, "the module should follow the host's tempo");
    CHECK(Rms(out, 3.5, 6.0) > 0.05, "the loop should be audible");
}

/** A MIDI CC moves the firmware's value; the host parameter follows. */
void TestMidiToHost()
{
    ScotykachAudioProcessor plugin;
    PlayHead head;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(kRate, kBlock);
    Run(plugin, head, 0.1, 1e9, [&](double t, juce::MidiBuffer& midi) {
        if (t == 0.0) midi.addEvent(juce::MidiMessage::controllerEvent(1, 22, 64), 100);   // CC 22, Size A
    });
    plugin.syncParametersToHost();
    CHECK(std::abs(GetParam(plugin, "a_size") - 64 / 127.f) < 1e-4, "A Size %.4f", GetParam(plugin, "a_size"));
}

/** Saved state brings back the parameters and both loops. */
void TestState(ScotykachAudioProcessor& recorded)
{
    SetParam(recorded, "b_mode", 2);   // Drift
    juce::MemoryBlock state;
    recorded.getStateInformation(state);
    const auto frames = StateOf(recorded).frames;
    std::printf("  state: %.2f MB for a %.2f s loop\n", (double)state.getSize() / 1048576.0, (double)frames / kFs);
    CHECK(state.getSize() > frames * 8 && state.getSize() < frames * 8 + 200000, "state size %zu", state.getSize());

    ScotykachAudioProcessor restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());
    CHECK(GetParam(restored, "b_mode") == 2.f, "B Mode %.0f", GetParam(restored, "b_mode"));
    CHECK(GetParam(restored, "a_mix") == 1.f, "A Mix %.2f", GetParam(restored, "a_mix"));

    std::vector<float> a(2 * frames), b(2 * frames);
    recorded.core().module().control([&](Panel& p, Core&) { p.readLoop(Deck::A, a.data(), frames); });
    const auto got = restored.core().module().control([&](Panel& p, Core&) { return p.readLoop(Deck::A, b.data(), frames); });
    CHECK(got == frames, "restored loop %zu frames, saved %zu", got, frames);
    CHECK(a == b, "the restored loop should be the saved one");

    // It plays when Play is pressed, as on the module after loading.
    PlayHead head;
    restored.setPlayHead(&head);
    restored.prepareToPlay(kRate, kBlock);
    const auto out = Run(restored, head, 2.0, 0.0, [&](double t, juce::MidiBuffer&) {
        if (t == 0.0) SetParam(restored, "a_play", 1);
        if (t > 0.05 && t < 0.07) SetParam(restored, "a_play", 0);
    });
    CHECK(Rms(out, 0.5, 2.0) > 0.05, "the restored loop should play (rms %.3f)", Rms(out, 0.5, 2.0));

    // A state that isn't ours changes nothing.
    const char junk[] = "not a state";
    restored.setStateInformation(junk, sizeof junk);
    CHECK(StateOf(restored).frames == frames, "junk state left the loop alone");
}

/** Mono input feeds both channels. */
void TestMono()
{
    ScotykachAudioProcessor plugin;
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add(juce::AudioChannelSet::mono());
    mono.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(plugin.setBusesLayout(mono), "mono in, stereo out should be supported");
    PlayHead head;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(kRate, kBlock);
    SetParam(plugin, "a_mix", 0);   // dry
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    double l = 0, r = 0;
    for (int b = 0; b < 50; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { buf.setSample(0, i, std::sin((float)i * 0.05f)); buf.setSample(1, i, 0.f); }
        plugin.processBlock(buf, midi);
        for (int i = 0; i < kBlock; ++i) { l += std::abs(buf.getSample(0, i)); r += std::abs(buf.getSample(1, i)); }
    }
    CHECK(l > 1.0 && std::abs(l - r) < 0.01 * l, "mono input should reach both sides (L %.1f, R %.1f)", l, r);
}

/** Bypass delays the dry signal by the latency. */
void TestBypass()
{
    ScotykachAudioProcessor plugin;
    plugin.prepareToPlay(kRate, kBlock);
    const int lat = plugin.getLatencySamples();
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    buf.clear();
    buf.setSample(0, 10, 1.f);
    plugin.processBlockBypassed(buf, midi);
    int at = -1;
    for (int i = 0; i < kBlock; ++i) if (buf.getSample(0, i) == 1.f) at = i;
    CHECK(at == 10 + lat, "the bypassed impulse should arrive at %d, came at %d", 10 + lat, at);
}

/** The window, as reported: Record, record 10 s, Close loop, stop the
 *  input. The loop plays on at its level (After Recording: Play); Record
 *  then overdubs and stops overdubbing; Clear empties the deck. */
void TestEditorRecord()
{
    ScotykachAudioProcessor plugin;
    PlayHead head;
    head.playing = false;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(kRate, kBlock);
    std::unique_ptr<juce::AudioProcessorEditor> ed(plugin.createEditor());
    auto* editor = dynamic_cast<ScotykachEditor*>(ed.get());
    CHECK(editor != nullptr, "the window should be the Scotykach editor");
    if (editor == nullptr) return;
    auto& record = *editor->pad("a_record");
    CHECK(editor->pad("alt") == nullptr && editor->pad("spot") == nullptr, "no Alt or Spot in the window");

    using DS = scotykach::Processor::DeckState;
    std::vector<DS> seen;
    std::vector<juce::String> labels;
    auto click = [&](ScotykachEditor::Pad& pd) { pd.press(); pd.release(); };   // quicker than a block
    auto note  = [&] { editor->refresh(); seen.push_back(plugin.core().deckStatus(0).state); labels.push_back(record.getText()); };
    const auto out = Run(plugin, head, 75.0, 10.5, [&](double t, juce::MidiBuffer&) {
        auto at = [&](double when) { return std::abs(t - when) < 0.5 * kBlock / (double)kRate; };   // one block
        if (at(0.2))  note();                  // Empty, "Record"
        if (at(0.5))  click(record);
        if (at(1.0))  note();                  // Recording, "Close loop"
        if (at(10.5)) click(record);
        if (at(11.0)) note();                  // Playing, "Overdub"
    });
    // SCOTYKACH_SNAPSHOT=file.png saves the window as it looks now.
    if (const char* path = std::getenv("SCOTYKACH_SNAPSHOT"))
    {
        editor->pad("a_grit")->press();
        Run(plugin, head, 0.2, 0.0, [](double, juce::MidiBuffer&) {});
        editor->refresh();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.f);
        juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(path));
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(image, stream);
        editor->pad("a_grit")->release();
    }

    const auto st = plugin.core().deckStatus(0);
    std::printf("  window: states %d %d %d, labels %s / %s / %s, loop %.2f s, rms 15-20 s %.3f, 70-75 s %.3f\n",
                (int)seen[0], (int)seen[1], (int)seen[2], labels[0].toRawUTF8(), labels[1].toRawUTF8(),
                labels[2].toRawUTF8(), st.seconds, Rms(out, 15, 20), Rms(out, 70, 75));
    CHECK(seen[0] == DS::Empty && labels[0] == "Record", "starts empty");
    CHECK(seen[1] == DS::Recording && labels[1] == "Close loop", "Record records (the input is playing)");
    CHECK(seen[2] == DS::Playing && labels[2] == "Overdub", "Close loop plays the loop, without overdubbing");
    CHECK(std::abs(st.seconds - 10.0) < 0.3, "a 10 s loop (%.2f s)", st.seconds);
    CHECK(Rms(out, 15, 20) > 0.05 && Rms(out, 70, 75) > 0.97 * Rms(out, 15, 20), "the loop should hold its level");

    // Overdub, Stop overdub, Clear.
    seen.clear();
    labels.clear();
    Run(plugin, head, 1.5, 0.0, [&](double t, juce::MidiBuffer&) {
        auto at = [&](double when) { return std::abs(t - when) < 0.5 * kBlock / (double)kRate; };   // one block
        if (at(0.1)) click(record);
        if (at(0.4)) note();                   // Overdubbing, "Stop overdub"
        if (at(0.5)) click(record);
        if (at(0.8)) note();                   // Playing
        if (at(1.0)) click(*editor->pad("a_clear"));
        if (at(1.3)) note();                   // Empty
    });
    CHECK(seen[0] == DS::Overdubbing && labels[0] == "Stop overdub", "Record overdubs (%d)", (int)seen[0]);
    CHECK(seen[1] == DS::Playing, "Stop overdub plays on (%d)", (int)seen[1]);
    CHECK(seen[2] == DS::Empty && plugin.core().deckStatus(0).seconds == 0.f, "Clear empties the deck (%d)", (int)seen[2]);
}

/** After Recording "Overdub" closes into overdubbing, as the module does. */
void TestEditorOverdubAfter()
{
    ScotykachAudioProcessor plugin;
    PlayHead head;
    head.playing = false;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(kRate, kBlock);
    std::unique_ptr<juce::AudioProcessorEditor> ed(plugin.createEditor());
    auto& editor = *dynamic_cast<ScotykachEditor*>(ed.get());
    editor.toggle("after_rec")->toggle();
    CHECK(GetParam(plugin, "after_rec") == 1.f, "the switch sets Overdub");
    auto& record = *editor.pad("a_record");
    Run(plugin, head, 3.0, 2.5, [&](double t, juce::MidiBuffer&) {
        auto at = [&](double when) { return std::abs(t - when) < 0.5 * kBlock / (double)kRate; };   // one block
        if (at(0.5) || at(2.5)) { record.press(); record.release(); }
    });
    CHECK(plugin.core().deckStatus(0).state == scotykach::Processor::DeckState::Overdubbing,
          "the closed loop overdubs (%d)", (int)plugin.core().deckStatus(0).state);
}

/** The lock switches follow the firmware, and an action mapped in the host
 *  (set to 1 and left there, as Live's key mapping does) acts every time. */
void TestEditorSwitchesAndMapping()
{
    ScotykachAudioProcessor plugin;
    PlayHead head;
    head.playing = false;
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(kRate, kBlock);
    std::unique_ptr<juce::AudioProcessorEditor> ed(plugin.createEditor());
    auto& editor = *dynamic_cast<ScotykachEditor*>(ed.get());
    auto gritOn = [&] {
        return plugin.core().module().control([](Panel&, Core& c) { return c.deck(Deck::A).fx().is_grit_on(); });
    };
    auto idle = [&](double s) { Run(plugin, head, s, 0.0, [](double, juce::MidiBuffer&) {}); plugin.syncParametersToHost(); };

    editor.toggle("a_grit_lock")->toggle();
    idle(0.2);
    CHECK(gritOn(), "Grit lock turns Grit on");
    // Alt + Grit on the pads (the Alt parameter) unlocks it; the switch follows.
    SetParam(plugin, "alt", 1); idle(0.05);
    SetParam(plugin, "a_grit", 1); idle(0.05);
    SetParam(plugin, "a_grit", 0); SetParam(plugin, "alt", 0); idle(0.2);
    CHECK(!gritOn() && GetParam(plugin, "a_grit_lock") == 0.f && !editor.toggle("a_grit_lock")->isOn(),
          "unlocked by the pads, the switch shows Off (%.0f)", GetParam(plugin, "a_grit_lock"));

    // Record mapped to a key: each press sets it to 1, and it goes back to 0.
    auto press = [&] { SetParam(plugin, "a_record", 1); idle(0.3); };
    press();
    const auto armed = plugin.core().deckStatus(0).state;
    CHECK(GetParam(plugin, "a_record") == 0.f, "Record goes back to 0 (%.0f)", GetParam(plugin, "a_record"));
    press();
    const auto cancelled = plugin.core().deckStatus(0).state;
    CHECK(armed != scotykach::Processor::DeckState::Empty && cancelled == scotykach::Processor::DeckState::Empty,
          "two presses arm then cancel (%d, %d)", (int)armed, (int)cancelled);
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::printf("plugin_test\n");
    TestParameters();
    {
        ScotykachAudioProcessor plugin;
        PlayHead head;
        TestRecordAndClock(plugin, head);
        TestState(plugin);
    }
    TestMidiToHost();
    TestMono();
    TestBypass();
    TestEditorRecord();
    TestEditorOverdubAfter();
    TestEditorSwitchesAndMapping();
    std::printf(test::Failures() ? "FAILED (%d)\n" : "ok\n", test::Failures());
    return test::Failures() != 0;
}
