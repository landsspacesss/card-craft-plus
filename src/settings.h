// =============================================================================
//  settings.h — the values behind the settings card
//
//  Separated from main.cpp because it is arithmetic rather than plumbing: what
//  is a legal brightness, what a stored byte is turned into, and how a field of
//  view becomes the pair of numbers the projection needs. None of that needs a
//  panel, a card or NVS, which is what lets the host tests pin it -- including
//  the case that matters most, a card whose NVS holds a value this build never
//  wrote.
//
//  The NVS read and write themselves stay in main.cpp, next to the Preferences
//  handle that owns every other key. This file may not include Arduino.h.
// =============================================================================
#pragma once

#include <stdint.h>

namespace settings {

// Brightness is a percentage in tens so the card can step it with one key;
// 10 is the floor because below it the card that got you here is unreadable.
constexpr uint8_t kBrightnessMin  = 10;
constexpr uint8_t kBrightnessMax  = 100;
constexpr uint8_t kBrightnessStep = 10;

// Field of view, in degrees. 60 is tight and 90 is about as wide as a panel
// this shape takes before the corners stop meaning anything.
constexpr uint8_t kFovMin  = 60;
constexpr uint8_t kFovMax  = 90;
constexpr uint8_t kFovStep = 5;

// Three sensitivity levels. Named kSens*, not an enum with LOW/MED/HIGH in it:
// Arduino.h defines LOW and HIGH as macros, so a header included after it cannot
// spell those as identifiers at all.
constexpr uint8_t kSensLow   = 0;
constexpr uint8_t kSensMed   = 1;
constexpr uint8_t kSensHigh  = 2;
constexpr uint8_t kSensCount = 3;

// Which measured axis each half of the look control comes from.
//
// This is a setting and not two constants because the answer is a property of
// how the part is soldered to the board, and the first real device answered it
// the hard way: the panel's normal axis (Z) is the one a steering-wheel twist
// turns, while the axis a player means by "look left" is the one lying along the
// world's vertical. The default here is the corrected pair, Y for turn; Z stays
// reachable so a board that turns out to be mounted differently is a keypress
// and not a reflash.
constexpr uint8_t kTurnYPlus  = 0;
constexpr uint8_t kTurnYMinus = 1;
constexpr uint8_t kTurnZPlus  = 2;
constexpr uint8_t kTurnZMinus = 3;
constexpr uint8_t kTurnCount  = 4;

constexpr uint8_t kTiltXPlus  = 0;
constexpr uint8_t kTiltXMinus = 1;
constexpr uint8_t kTiltCount  = 2;

// What the first board needed: the axis is Y (settled on the device) and the
// direction is negative. The device answered the second half a build later --
// turning the body left moved the view right -- which is exactly why this is a
// value the card can change rather than a sign compiled into the integration.
constexpr uint8_t kTurnDefault = kTurnYMinus;
constexpr uint8_t kTiltDefault = kTiltXPlus;

// Bumped whenever kTurnDefault/kTiltDefault change meaning. A card that stored
// a mapping under the old default would otherwise keep the old answer forever,
// and the fix would look like it did nothing.
constexpr uint8_t kMappingVersion = 2;

struct Settings {
  uint8_t brightnessPct = 60;    // ≈ the 160/255 this used to be hardcoded to
  uint8_t fovDeg        = 75;    // ≈ the 0.75 plane the renderer shipped with
  bool    gyroOn        = false; // off out of the box: it costs power and drifts
  uint8_t gyroSens      = kSensMed;
  uint8_t gyroTurn      = kTurnDefault;   // Y-
  uint8_t gyroTilt      = kTiltDefault;   // X+
};

// Forces every field into the range this build understands. Everything that
// comes from NVS goes through here, and so does every change the card makes.
void sanitize(Settings& s);

// A Settings from four stored bytes, sanitised. Kept as a function rather than
// four reads in main.cpp so the clamping is what the tests exercise.
Settings decode(uint8_t br, uint8_t fov, uint8_t gyr, uint8_t gs,
                uint8_t gt = kTurnDefault, uint8_t gp = kTiltDefault);

// What the panel's backlight is actually asked for, 0..255.
uint8_t brightnessDuty(const Settings& s);

// The field of view is turned into a camera plane by raycast::setFovDeg(); the
// mapping lives there because that is the only place it is used, and a second
// copy here would be a copy the tests check instead of the one that runs.

}  // namespace settings
