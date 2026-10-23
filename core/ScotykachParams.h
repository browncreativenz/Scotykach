/**
 * @file ScotykachParams.h
 * @brief Every plugin parameter: what it is, its ID, and its default.
 *
 * Plain C++ (no firmware or JUCE headers), so the plugin can build its host
 * parameters from it. Values are plain values:
 *
 *   Unit    0..1, a knob position, as on the module (the firmware applies
 *           its own curves)
 *   Cv      -1..1, a CV input (±5 V)
 *   Choice  0..count-1, a switch position
 *   Button  0 or 1, a pad, the Tap button, or a gate input: 1 is held
 *
 * IDs are stable: they are saved with the plugin state, so never change one.
 * Defaults are the module's factory values (CoreUI::_init_values and the
 * FX defaults); tests/params_test.cpp checks them against a booted module.
 */

#pragma once

#include <cstddef>

namespace scotykach::params {

enum class Kind { Unit, Cv, Choice, Button };

struct Info
{
    const char*        id;
    const char*        name;
    Kind               kind;
    float              defaultValue;
    const char* const* choices    = nullptr;   // Choice only
    int                numChoices = 0;
};

constexpr int kDecks = 2;

/** Per-deck pot controls, in Panel::Control order. */
constexpr int kDeckControls = 17;
/** Shared pot controls, in Panel::Global order. */
constexpr int kGlobals = 6;
/** CV inputs, in Hardware::CvInputId order. */
constexpr int kCvInputs = 7;

/** Switches. */
enum Switch { kRoute, kModeA, kModeB, kModTypeA, kModTypeB, kCvRouteA, kCvRouteB, kClock, kSwitches };

/** Buttons: the twelve pads, Tap, and the two gate inputs. */
enum Button
{
    kPlayA, kRevA, kGritA, kFluxA, kSeqA,
    kPlayB, kRevB, kGritB, kFluxB, kSeqB,
    kSpot, kAlt, kTap, kGateA, kGateB,
    kButtons
};

/** Where each group starts in the flat parameter list. */
constexpr int kDeckBase   = 0;
constexpr int kGlobalBase = kDeckBase + kDecks * kDeckControls;
constexpr int kCvBase     = kGlobalBase + kGlobals;
constexpr int kSwitchBase = kCvBase + kCvInputs;
constexpr int kButtonBase = kSwitchBase + kSwitches;
constexpr int kCount      = kButtonBase + kButtons;

constexpr int DeckControl(int deck, int control) { return kDeckBase + deck * kDeckControls + control; }
constexpr int GlobalControl(int g)               { return kGlobalBase + g; }
constexpr int CvInput(int cv)                    { return kCvBase + cv; }
constexpr int SwitchParam(Switch s)              { return kSwitchBase + s; }
constexpr int ButtonParam(Button b)              { return kButtonBase + b; }

/** Clock choices. */
enum Clock { kClockLive, kClockInternal };

/** Parameter p (0 <= p < kCount). */
const Info& Get(int p);

/** Clamps v to the parameter's range (rounding choices and buttons). */
float Clamp(int p, float v);

} // namespace scotykach::params
