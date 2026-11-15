// =============================================================================
//  motion_math.cpp — what a gyro rate means, with no sensor in the picture
//
//  Split out of motion.cpp so the host tests can reach it: the driver needs an
//  I2C bus and a Bosch part, and none of this arithmetic does.
// =============================================================================
#include "motion.h"

#include <math.h>

#include "raycast.h"

namespace motion {

float deadbandDps(float dps) {
  const float mag = fabsf(dps);
  if (mag <= DEADBAND_DPS) return 0.0f;
  const float over = mag - DEADBAND_DPS;
  return dps < 0.0f ? -over : over;
}

float sensFactor(uint8_t level) {
  switch (level) {
    case 0: return 0.5f;   // LOW
    case 2: return 1.8f;   // HIGH
    default: return 1.0f;  // MED, and anything unrecognised
  }
}

static float toRadians(float dps) { return dps * 3.14159265f / 180.0f; }

float pickAxis(uint8_t axis, float gx, float gy, float gz) {
  switch (axis) {
    case 0: return gx;
    case 1: return gy;
    case 2: return gz;
    default: return 0.0f;
  }
}

const char* turnName(uint8_t map) {
  static const char* const kName[4] = { "Y+", "Y-", "Z+", "Z-" };
  return kName[map < 4 ? map : 0];
}

const char* tiltName(uint8_t map) {
  static const char* const kName[2] = { "X+", "X-" };
  return kName[map < 2 ? map : 0];
}

// Which measured axis each control takes, and which way round. Kept as a table
// rather than three signs in three places: the first real device proved the
// guess about the axes wrong, and a wrong guess must be one keypress.
static float mappedDps(float gx, float gy, float gz, uint8_t map) {
  // For turning: 0 = +Y, 1 = -Y, 2 = +Z, 3 = -Z.
  const uint8_t axis = (map < 2) ? 1 : 2;
  const float sign = (map == 0 || map == 2) ? 1.0f : -1.0f;
  return sign * pickAxis(axis, gx, gy, gz);
}

float turnRateRad(float gx, float gy, float gz, uint8_t map, uint8_t level) {
  return deadbandDps(mappedDps(gx, gy, gz, map)) * sensFactor(level) *
         (3.14159265f / 180.0f);
}

float tiltRateRows(float gx, float gy, float gz, uint8_t map, uint8_t level) {
  // The tilt is always measured on X, whichever sign the board needs.
  const float sign = (map == 0) ? 1.0f : -1.0f;
  // Rows per radian is the projection: the shear through a screen row is
  // (VIEW_H/2 - horizon)/proj, so one radian of pitch moves the horizon by
  // proj rows at level. Reading it from raycast keeps that proportional to
  // whatever field of view is set.
  return toRadians(deadbandDps(sign * gx) * sensFactor(level)) * raycast::proj();
}

float averageBias(const float* samples, int n) {
  if (samples == nullptr || n <= 0) return 0.0f;
  float sum = 0.0f;
  for (int i = 0; i < n; ++i) sum += samples[i];
  return sum / (float)n;
}

float applyBias(float dps, float bias) { return dps - bias; }

}  // namespace motion
