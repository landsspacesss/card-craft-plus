// =============================================================================
//  settings.cpp — see settings.h
// =============================================================================
#include "settings.h"

namespace settings {

static uint8_t clampTo(uint8_t v, uint8_t lo, uint8_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void sanitize(Settings& s) {
  // Brightness floors to a step rather than rounding: a stored 64 was 60% when
  // it was written, and taking it back to 60 is the honest reading.
  s.brightnessPct = (uint8_t)(clampTo(s.brightnessPct, kBrightnessMin, kBrightnessMax) /
                              kBrightnessStep * kBrightnessStep);

  // FOV rounds to the nearest step, because it only ever moves in steps and a
  // stored value between two of them came from arithmetic, not a player.
  uint8_t f = clampTo(s.fovDeg, kFovMin, kFovMax);
  f = (uint8_t)(((f - kFovMin + kFovStep / 2) / kFovStep) * kFovStep + kFovMin);
  s.fovDeg = clampTo(f, kFovMin, kFovMax);

  // An unrecognised sensitivity is not clamped to a neighbour: it is MED, the
  // level that moves 1:1 with the hand, because that is the one a player who
  // never touched the row expects.
  if (s.gyroSens >= kSensCount) s.gyroSens = kSensMed;
  if (s.gyroTurn >= kTurnCount) s.gyroTurn = kTurnDefault;
  if (s.gyroTilt >= kTiltCount) s.gyroTilt = kTiltDefault;
}

Settings decode(uint8_t br, uint8_t fov, uint8_t gyr, uint8_t gs,
                uint8_t gt, uint8_t gp) {
  Settings s;
  s.brightnessPct = br;
  s.fovDeg        = fov;
  s.gyroOn        = gyr != 0;
  s.gyroSens      = gs;
  s.gyroTurn      = gt;
  s.gyroTilt      = gp;
  sanitize(s);
  return s;
}

uint8_t brightnessDuty(const Settings& s) {
  // Never 0: the check is redundant with sanitize(), and it is here anyway
  // because a multiply that can reach zero is the difference between a dim card
  // and a black screen with no way back to the card.
  const uint16_t duty = (uint16_t)s.brightnessPct * 255u / 100u;
  return duty == 0 ? 1 : (uint8_t)duty;
}

}  // namespace settings
