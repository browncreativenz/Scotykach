/**
 * @file ScotykachEngine.cpp
 * @brief See ScotykachEngine.h.
 */

#include "ScotykachEngine.h"

#include <cmath>
#include <cstdlib>
#include <new>

#include "hw/buffer.sdram.h"

namespace scotykach {

using namespace spotykach;

namespace {

/* On the module the engine is a static object, so memory its constructors
 * leave alone starts at zero (Core never sets _route, for one). Building it
 * in zeroed memory keeps that, and keeps renders repeatable. */
Core* NewZeroedCore()
{
    void* mem = std::calloc(1, sizeof(Core));
    if (mem == nullptr) throw std::bad_alloc();
    return new (mem) Core();
}

} // namespace

void Engine::ZeroedDelete::operator()(Core* c) const
{
    c->~Core();
    std::free(c);
}

Engine::Engine()
    : pool(std::make_unique<SDRAMBuffer>())
{
    ctx.pool = pool.get();

    shim::Context::Scope scope(ctx);
    speedMap.init();
    core.reset(NewZeroedCore());
    core->init((float)kSampleRate, (float)kBlock);
    applyFactorySettings();
}

Engine::~Engine() = default;

void Engine::applyFactorySettings()
{
    // CoreUI installs these; without the UI they do nothing.
    core->driver().set_on_quarter([](bool) {});
    core->driver().set_on_clock_out([] {});

    // The panel switches in their centre positions (CoreUI::_process_switches).
    core->set_route(Route::Stereo);
    core->mod(Deck::A).set_type(Modulator::Type::LFO);
    core->mod(Deck::A).set_lfo_type(LFO::Type::random);
    core->mod(Deck::B).set_type(Modulator::Type::LFO);
    core->mod(Deck::B).set_lfo_type(LFO::Type::sine);
    for (auto ref : { Deck::A, Deck::B })
    {
        auto& deck = core->deck(ref);
        deck.set_mode(Mode::Slice);
        deck.voxs().set_start_mod_on(true);
        deck.voxs().set_size_mod_on(true);
    }
    core->infer_panner_mode();

    // The factory values (CoreUI::_init_values), applied as CoreUI::process
    // applies them on its first pass, before any mode has taken effect.
    for (auto ref : { Deck::A, Deck::B })
    {
        auto& deck = core->deck(ref);
        auto& g    = deck.voxs();
        g.set_start(0.f);
        g.set_start_offset_interval(0.f);
        g.set_size(1.f, false);
        g.set_shape(0.f);
        g.set_speed(.5f);
        g.set_env_size(1.f);
        g.set_win_size(.2f);
        deck.set_inout_mix(.5f);
        deck.set_feedback(kDefaultFeedback);

        auto& fx = deck.fx();
        fx.grit().set_mix(fx.grit().mix());
        fx.grit().set_intensity(fx.grit().intensity());
        fx.grit().set_character(fx.grit().character());
        fx.flux().set_mix_norm(fx.flux().mix_norm());
        fx.flux().set_time_norm(fx.flux().time_norm());
        fx.flux().set_fb_norm(fx.flux().fb_norm());

        core->mod(ref).set_speed_norm(.3f, false);
        core->mod(ref).set_amp_norm(0.f);
    }
    core->panner().set_range(.6f);
    core->panner().set_speed(1.f);
    core->set_click_mix(0.f);
    core->set_mix(.5f);
    core->driver().set_key_tick_interval_norm(.0588f);   // a quarter note
    core->driver().set_tempo_norm(Tempo::abs_to_norm(120.f));
}

void Engine::process(const float* inL, const float* inR, float* outL, float* outR, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
    {
        const float xl = inL[i], xr = inR[i];
        outL[i] = fifoOutL[fifoPos];
        outR[i] = fifoOutR[fifoPos];
        fifoInL[fifoPos] = xl;
        fifoInR[fifoPos] = xr;

        if (++fifoPos == (std::size_t)kBlock)
        {
            runBlock();
            fifoPos = 0;
        }
    }
}

void Engine::runBlock()
{
    shim::Context::Scope scope(ctx);

    // TIM5 (T5Callback): the generators' block preparation.
    core->prepare();

    // The audio callback (AppImpl::ProcessAudio): CoreUI::tick drives the
    // clock, here with no external clock edge; then the CV inputs.
    core->driver().tick(false);
    readCv();

    const float* in[2]  = { fifoInL, fifoInR };
    float*       out[2] = { fifoOutL, fifoOutR };
    core->process(in, out, kBlock);

    // The DAC callback runs at the audio rate (DACCallback in app.cpp).
    for (int i = 0; i < kBlock; ++i)
    {
        core->mod(Deck::A).process(modValue[Deck::A]);
        core->mod(Deck::B).process(modValue[Deck::B]);
    }

    ctx.samples += kBlock;
    ++blockCount;
}

void Engine::readCv()
{
    // As CoreUI::read_cv, from already-calibrated readings.
    for (auto ref : { Deck::A, Deck::B })
    {
        auto& deck = core->deck(ref);
        deck.inout_mix_mod_in(cv.mix[ref]);
        const float posSize = std::round(cv.posSize[ref] * 1000.f) / 1000.f;
        deck.voxs().set_start_mod(posSize);
        deck.voxs().set_size_mod(posSize);
        deck.voxs().pitch_speed_mod_in(speedMap.bipolar_pitch2speed(cv.voct[ref]));
    }
    core->mix_mod_in(cv.crossfade);
}

} // namespace scotykach
