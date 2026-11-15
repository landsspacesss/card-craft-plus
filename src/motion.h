// =============================================================================
//  motion.h — the gyroscope, split where the hardware stops
//
//  Two halves with one header because they are one feature:
//
//   * the ARITHMETIC (deadbandDps, sensFactor, yawRateRad, pitchRateRows,
//     averageBias) is pure and lives in motion_math.cpp, which the host tests
//     compile. It decides what a rate means, and it is where every mistake
//     would be a game-feel bug nobody could test on a desk.
//
//   * the DRIVER (begin/available/readRates/calibrate/end) talks to a Bosch
//     BMI270 and lives in motion.cpp, which only the device build compiles.
//
//  Nothing here includes Arduino or M5Unified: a board with no IMU at all
//  compiles the same header and simply never calls begin().
// =============================================================================
#pragma once

#include <stdint.h>

namespace motion {

// Below this, a rate is the sensor's own noise rather than a hand. A BMI270 at
// rest is quoted at 0.5-9 deg/s of zero-rate offset, so the band has to swallow
// more than a hand would ever intend.
constexpr float DEADBAND_DPS = 2.0f;

// Which way is which. These exist because the board's axes are a property of
// how the part is soldered down, and the two that matter (roll about the
// screen's normal for yaw, pitch about the horizontal axis) can only be
// confirmed on the real thing. Flipping a sign here is the whole fix; it must
// not be scattered through the integration.
constexpr float YAW_SIGN   = 1.0f;
constexpr float PITCH_SIGN = 1.0f;

// ---- arithmetic (motion_math.cpp) -------------------------------------------

// Zero inside the band, and continuous at its edge: 2.5 deg/s is 0.5, not 2.5,
// because a value that jumps when it leaves the band reads as a click.
float deadbandDps(float dps);

// 0.5 / 1.0 / 1.8 for LOW / MED / HIGH. Anything else is MED.
float sensFactor(uint8_t level);

// A rate in deg/s turned into what the simulation takes: radians per second for
// the camera's angle, horizon rows per second for its pitch.
float yawRateRad(float dps, uint8_t level);
float pitchRateRows(float dps, uint8_t level);

// What a still sensor reads, averaged; and one sample with it removed.
float averageBias(const float* samples, int n);
float applyBias(float dps, float bias);

// ---- driver (motion.cpp, device builds only) ---------------------------------

// Brings the sensor up, in this order: M5Unified's own IMU if M5.begin() left
// it enabled, then M5Unified's IMU started explicitly, then a BMI270 addressed
// directly at 0x68 and 0x69. Returns false if all three fail, which is a board
// without the part rather than an error to report loudly.
bool begin();

// Started and usable. False after a failed begin() and after end().
bool available();

// Stops sampling and puts the part to sleep. The sensor stays powered -- it
// shares the keyboard's I2C bus and has no enable pin -- but it stops
// interrupting anything and stops being read, which is what the setting buys.
void end();

// Averages about half a second of a still board into the zero-rate offset. Only
// meaningful while nobody is touching it; the card says so on screen.
void calibrate();

// Rates in deg/s with the calibrated offset removed. False when there is
// nothing to read, in which case the references are untouched.
bool readRates(float& yawDps, float& pitchDps);

#ifdef CRAFTCRAFT_GYRO_FAKE
// Emulator only: QEMU does not model the BMI270, so the tour build injects the
// rates a hand would have produced and everything downstream runs for real.
void fakeSet(float yawDps, float pitchDps);
#endif

}  // namespace motion
