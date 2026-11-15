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
float   s_biasX = 0.0f;
float   s_biasY = 0.0f;
float   s_biasZ = 0.0f;
m5::BMI270_Class* s_direct = nullptr;

#ifdef CRAFTCRAFT_GYRO_FAKE
float s_fakeX = 0.0f;
float s_fakeY = 0.0f;
float s_fakeZ = 0.0f;
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
  // Already sampling: return, and do not touch the part. applySettings() calls
  // this on every settings change, and on the directly-addressed path a real
  // begin() soft-resets the sensor and re-uploads its 8 KB configuration --
  // about a fifth of a second, per keypress -- as well as replacing the object.
  if (s_ok && !s_asleep) return true;

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

  // A previous direct attempt that has since been slept: drop it before making
  // another, or the object it points at leaks once per wake-up.
  delete s_direct;
  s_direct = nullptr;

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
  if (s_asleep) return;
  // Deliberately not gated on s_ok. M5.begin() starts the IMU itself when the
  // internal one is enabled and In_I2C came up, so with the setting off and
  // begin() never called there is still a sensor sampling -- which is the one
  // this is meant to be saving power on.
  //
  // The BMI270 driver in this library does not implement IMU_Base::sleep(), so
  // the directly-addressed path can only stop being read; the M5Unified one
  // does, and that is the path a board this old usually takes.
  if (s_src == Src::DIRECT && s_direct != nullptr) s_direct->sleep();
  else if (M5.Imu.isEnabled())                     M5.Imu.sleep();
  s_asleep = true;
  s_ok = false;
#endif
}

bool readRaw(float& gx, float& gy, float& gz) {
  if (!s_ok || s_asleep) return false;

#ifdef CRAFTCRAFT_GYRO_FAKE
  gx = applyBias(s_fakeX, s_biasX);
  gy = applyBias(s_fakeY, s_biasY);
  gz = applyBias(s_fakeZ, s_biasZ);
  return true;
#else
  float rx = 0.0f, ry = 0.0f, rz = 0.0f;
  if (!readGyroDps(rx, ry, rz)) return false;
  // All three axes, unfiltered and unmapped: which of them means "turn" is a
  // question about the soldering, and it is answered on the check screen by
  // watching which number moves -- not here.
  gx = applyBias(rx, s_biasX);
  gy = applyBias(ry, s_biasY);
  gz = applyBias(rz, s_biasZ);
  return true;
#endif
}

bool calibrate() {
  if (!s_ok || s_asleep) return false;

#ifdef CRAFTCRAFT_GYRO_FAKE
  s_biasX = 0.0f;
  s_biasY = 0.0f;
  s_biasZ = 0.0f;
  return true;
#else
  float xs[CAL_SAMPLES];
  float ys[CAL_SAMPLES];
  float zs[CAL_SAMPLES];
  int n = 0;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    // A failed read is skipped rather than counted as zero: one zero in the
    // average pulls the offset and leaves a drift in the direction it came from.
    if (readGyroDps(gx, gy, gz)) {
      xs[n] = gx;
      ys[n] = gy;
      zs[n] = gz;
      ++n;
    }
    delay(CAL_GAP_MS);
  }
  if (n < CAL_SAMPLES / 2) return false;   // not enough samples to claim one
  s_biasX = averageBias(xs, n);
  s_biasY = averageBias(ys, n);
  s_biasZ = averageBias(zs, n);
  Serial.printf("motion: bias %.2f / %.2f / %.2f dps from %d samples\n",
                (double)s_biasX, (double)s_biasY, (double)s_biasZ, n);
  return true;
#endif
}

#ifdef CRAFTCRAFT_GYRO_FAKE
void fakeSet(float gx, float gy, float gz) {
  s_fakeX = gx;
  s_fakeY = gy;
  s_fakeZ = gz;
}
#endif

}  // namespace motion
