// =============================================================================
//  motion.cpp — the BMI270, and the three ways it might be there
//
//  The Cardputer ADV carries a Bosch BMI270 on the keyboard's I2C bus. Which
//  way M5Unified finds it depends on what the board detection did before this
//  ran, so there are three attempts, cheapest first:
//
//    1. M5.begin() already started an IMU (it does when the internal IMU is
//       enabled and In_I2C came up) -- just use it.
//    2. Start M5Unified's IMU explicitly. On this board that is the same BMI270
//       the keyboard shares the bus with, with the axis order the library
//       decides by board type.
//    3. Address the part directly at 0x68, then 0x69. This is not a second
//       driver: it is the same m5::BMI270_Class with an address chosen here
//       instead of by board detection, which matters because the ADV is not a
//       board every library version recognises. M5Unified already carries the
//       8 KB initialisation blob the part needs; writing another one out is how
//       a gyro ends up half-configured.
//
//  What is NOT here is any arithmetic: see motion_math.cpp for the deadband,
//  the sensitivity levels and the two unit conversions, all of which the host
//  tests pin.
// =============================================================================
#include "motion.h"

#include <M5Unified.h>
#include <utility/imu/BMI270_Class.hpp>

namespace motion {

namespace {

enum class Src : uint8_t { NONE, UNIFIED, DIRECT };

Src     s_src = Src::NONE;
bool    s_ok = false;
bool    s_asleep = false;
float   s_biasYaw = 0.0f;
float   s_biasPitch = 0.0f;
m5::BMI270_Class* s_direct = nullptr;

#ifdef CRAFTCRAFT_GYRO_FAKE
float s_fakeYaw = 0.0f;
float s_fakePitch = 0.0f;
#endif

constexpr int CAL_SAMPLES = 32;
constexpr int CAL_GAP_MS  = 15;   // ~0.5 s in total, as the card promises

// One gyro read in deg/s, from whichever of the three paths came up.
//
// The directly-addressed part is read through IMU_Base's raw interface rather
// than a getGyro() convenience method: in this library version the convenience
// methods are commented out and each driver exposes only getImuRawData() plus
// the conversion parameters, with IMU_Class doing the arithmetic for the ones
// it owns. Doing it here keeps the two paths returning the same units.
bool readGyroDps(float& gx, float& gy, float& gz) {
  if (s_src == Src::DIRECT && s_direct != nullptr) {
    m5::IMU_Base::imu_raw_data_t raw;
    m5::IMU_Base::imu_convert_param_t par;
    const auto spec = s_direct->getImuRawData(&raw);
    if (!(spec & m5::IMU_Base::imu_spec_gyro)) return false;
    s_direct->getConvertParam(&par);
    gx = (float)raw.gyro.x * par.gyro_res;
    gy = (float)raw.gyro.y * par.gyro_res;
    gz = (float)raw.gyro.z * par.gyro_res;
    return true;
  }
  return M5.Imu.getGyro(&gx, &gy, &gz);
}

}  // namespace

bool begin() {
#ifdef CRAFTCRAFT_GYRO_FAKE
  s_src = Src::UNIFIED;
  s_ok = true;
  s_asleep = false;
  return true;
#else
  if (s_asleep) {
    // sleep() leaves _imu set, so isEnabled() still says yes while the part is
    // powered down. Start over instead of trusting it.
    s_asleep = false;
    if (s_src == Src::DIRECT && s_direct != nullptr) {
      if (s_direct->begin(nullptr)) { s_ok = true; return true; }
    }
    if (M5.Imu.begin(&M5.In_I2C, M5.getBoard()) && M5.Imu.isEnabled()) {
      s_src = Src::UNIFIED;
      s_ok = true;
      return true;
    }
  }

  if (M5.Imu.isEnabled()) {
    s_src = Src::UNIFIED;
    s_ok = true;
    Serial.println("motion: gyro via M5Unified (already started)");
    return true;
  }
  if (M5.Imu.begin(&M5.In_I2C, M5.getBoard()) && M5.Imu.isEnabled()) {
    s_src = Src::UNIFIED;
    s_ok = true;
    Serial.println("motion: gyro via M5Unified");
    return true;
  }

  // The addresses disagree between sources, so both are tried rather than
  // guessed: 0x68 first, because that is the part's powered-up default.
  const uint8_t kAddr[2] = { 0x68, 0x69 };
  for (uint8_t addr : kAddr) {
    auto* part = new m5::BMI270_Class(addr, 400000, &M5.In_I2C);
    if (part->begin(nullptr)) {
      s_direct = part;
      s_src = Src::DIRECT;
      s_ok = true;
      Serial.printf("motion: gyro via BMI270 at 0x%02X\n", addr);
      return true;
    }
    delete part;
  }

  s_src = Src::NONE;
  s_ok = false;
  Serial.println("motion: no gyro");
  return false;
#endif
}

bool available() { return s_ok && !s_asleep; }

void end() {
#ifdef CRAFTCRAFT_GYRO_FAKE
  s_ok = false;
  s_asleep = true;
  return;
#else
  if (!s_ok) return;
  // The BMI270 driver in this library does not implement IMU_Base::sleep(), so
  // the directly-addressed path can only stop being read. The M5Unified one
  // does, and that is the path a board this old usually takes.
  if (s_src == Src::DIRECT && s_direct != nullptr) s_direct->sleep();
  else                                             M5.Imu.sleep();
  s_asleep = true;
#endif
}

bool readRates(float& yawDps, float& pitchDps) {
  if (!s_ok || s_asleep) return false;

#ifdef CRAFTCRAFT_GYRO_FAKE
  yawDps   = applyBias(s_fakeYaw, s_biasYaw);
  pitchDps = applyBias(s_fakePitch, s_biasPitch);
  return true;
#else
  float gx = 0.0f, gy = 0.0f, gz = 0.0f;
  if (!readGyroDps(gx, gy, gz)) return false;

  // Yaw is rotation about the axis out of the screen (the part's Z); pitch is
  // rotation about the horizontal one (its X). Which physical axis that is on
  // this board is a soldering question, not a software one -- if the first
  // real-device test turns the wrong way, YAW_SIGN / PITCH_SIGN in motion.h is
  // the whole fix. gy is deliberately unused: nothing here rolls.
  yawDps   = applyBias(gz, s_biasYaw);
  pitchDps = applyBias(gx, s_biasPitch);
  return true;
#endif
}

void calibrate() {
  if (!s_ok || s_asleep) return;

#ifdef CRAFTCRAFT_GYRO_FAKE
  s_biasYaw = 0.0f;
  s_biasPitch = 0.0f;
  return;
#else
  float yaw[CAL_SAMPLES];
  float pitch[CAL_SAMPLES];
  int n = 0;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    // A failed read is skipped rather than counted as zero: one zero in the
    // average pulls the offset and leaves a drift in the direction it came from.
    if (readGyroDps(gx, gy, gz)) {
      yaw[n]   = gz;
      pitch[n] = gx;
      ++n;
    }
    delay(CAL_GAP_MS);
  }
  if (n < CAL_SAMPLES / 2) return;   // not enough samples to claim a calibration
  s_biasYaw   = averageBias(yaw, n);
  s_biasPitch = averageBias(pitch, n);
  Serial.printf("motion: bias %.2f / %.2f dps from %d samples\n",
                (double)s_biasYaw, (double)s_biasPitch, n);
#endif
}

#ifdef CRAFTCRAFT_GYRO_FAKE
void fakeSet(float yawDps, float pitchDps) {
  s_fakeYaw = yawDps;
  s_fakePitch = pitchDps;
}
#endif

}  // namespace motion
