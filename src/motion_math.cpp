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

float yawRateRad(float dps, uint8_t level) {
  return deadbandDps(dps) * sensFactor(level) * (3.14159265f / 180.0f) * YAW_SIGN;
}

float pitchRateRows(float dps, uint8_t level) {
  // Rows per radian is the projection: the shear through a screen row is
  // (VIEW_H/2 - horizon)/proj, so one radian of pitch moves the horizon by
  // proj rows at level. Reading it from raycast keeps that proportional to
  // whatever field of view is set.
  return toRadians(deadbandDps(dps) * sensFactor(level)) * raycast::proj() * PITCH_SIGN;
}

float averageBias(const float* samples, int n) {
  if (samples == nullptr || n <= 0) return 0.0f;
  float sum = 0.0f;
  for (int i = 0; i < n; ++i) sum += samples[i];
  return sum / (float)n;
}

float applyBias(float dps, float bias) { return dps - bias; }

}  // namespace motion
