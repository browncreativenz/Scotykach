/**
 * @file ScotykachModule.cpp
 * @brief See ScotykachModule.h.
 */

#include "ScotykachModule.h"

#include <cstdlib>
#include <new>

#include "hw/buffer.sdram.h"
#include "memory/settings.h"
#include "memory/storage.h"
#include "ui/core.ui.h"

namespace scotykach {

using namespace spotykach;

/* AppImpl's members, in AppImpl's order (CoreUI only keeps references to
 * the ones after it). */
struct Module::Firmware
{
    Firmware() : ui(hw, core, settings, storage) {}

    Core     core;
    CoreUI   ui;
    Hardware hw;
    Settings settings;
    Storage  storage;
};

void Module::FirmwareDelete::operator()(Firmware* f) const
{
    f->~Firmware();
    std::free(f);
}

Module::Module()
    : pool(std::make_unique<SDRAMBuffer>())
{
    ctx.pool = pool.get();
    shim::Context::Scope scope(ctx);

    /* On the module these objects are statics, so whatever their
     * constructors leave alone starts at zero. Build them in zeroed memory
     * to match. */
    void* mem = std::calloc(1, sizeof(Firmware));
    if (mem == nullptr) throw std::bad_alloc();
    fw.reset(new (mem) Firmware());

    // AppImpl::Init, without the peripherals.
    fw->hw.Init((float)kSampleRate, kBlock);
    fw->core.init((float)kSampleRate, (float)kBlock);
    fw->ui.init();
    fw->storage.init(fw->core.deck(Deck::A), fw->core.deck(Deck::B));
    fw->storage.read_settigs();
    fw->settings.init(fw->hw);
    fw->settings.read();
    fw->ui.calibrate(false);

    panel = std::make_unique<Panel>(fw->ui, fw->core, fw->hw);

    // Skip the LED start-up, then one main-loop pass: CoreUI takes its
    // factory values, so values set on the panel from now on stay set.
    panel->finishBoot();
    fw->ui.process();
    panel->set(Panel::Global::Crossfade, .5f);   // a centred fader
}

Module::~Module()
{
    shim::Context::Scope scope(ctx);
    panel.reset();
    fw.reset();
}

Core& Module::core() { return fw->core; }

const Hardware& Module::hardware() const { return fw->hw; }

void Module::process(const float* inL, const float* inR, float* outL, float* outR, std::size_t n)
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

void Module::runBlock()
{
    shim::Context::Scope scope(ctx);
    auto& ui = fw->ui;

    if (hook) hook(ctx.samples, *panel);

    // The main loop (AppImpl::Loop).
    ui.process();
    fw->storage.process();

    // TIM5 (T5Callback).
    ui.process_gate_in();
    fw->core.prepare();
    if (timerCount++ == 3)
    {
        timerCount = 0;
        ui.render_leds();
    }

    // The audio callback (AppImpl::ProcessAudio).
    fw->hw.ProcessAnalogControls();
    ui.tick();
    ui.read_cv();
    const float* in[2]  = { fifoInL, fifoInR };
    float*       out[2] = { fifoOutL, fifoOutR };
    fw->core.process(in, out, kBlock);

    // The DAC callback, at the audio rate (DACCallback, AppImpl::mod_src).
    for (int i = 0; i < kBlock; ++i)
    {
        float a = 0.f, b = 0.f;
        fw->core.mod(Deck::A).process(a);
        fw->core.mod(Deck::B).process(b);
        ui.set_lfo(a, b);
        modValue[Deck::A] = a;
        modValue[Deck::B] = b;
    }

    ctx.samples += kBlock;
    ++blockCount;
}

} // namespace scotykach
