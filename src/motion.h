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

// Which measured axis is which. The first real device settled this: a gyro's Z
// is the axis out of the package's top face, which on this board is the axis a
// steering-wheel twist turns -- so it is NOT the one a player means by "look
// left". Turning about the world's vertical is the in-plane axis lying along the
// panel's short side, which is Y. The mapping is therefore a setting (see
// settings::gyroTurn/gyroTilt), not a pair of constants, because the answer is
// a property of the soldering and a wrong guess must cost a keypress rather than
// a reflash. These names are what the check screen prints.
const char* turnName(uint8_t map);   // "Y+", "Y-", "Z+", "Z-"
const char* tiltName(uint8_t map);   // "X+", "X-"

// One axis of one sample, in deg/s. axis: 0 = x, 1 = y, 2 = z; anything else 0.
float pickAxis(uint8_t axis, float gx, float gy, float gz);

// ---- arithmetic (motion_math.cpp) -------------------------------------------

// Zero inside the band, and continuous at its edge: 2.5 deg/s is 0.5, not 2.5,
// because a value that jumps when it leaves the band reads as a click.
float deadbandDps(float dps);

// 0.5 / 1.0 / 1.8 for LOW / MED / HIGH. Anything else is MED.
float sensFactor(uint8_t level);

// A raw three-axis sample and a mapping turned into what the simulation takes:
// radians per second for the camera's angle, horizon rows per second for its
// pitch. The mapping picks the axis AND its sign, so an inverted tilt is a
// setting rather than a patch.
float turnRateRad(float gx, float gy, float gz, uint8_t map, uint8_t level);
float tiltRateRows(float gx, float gy, float gz, uint8_t map, uint8_t level);

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

// Stops sampling and puts the part to sleep, whoever started it. The sensor
// stays powered -- it shares the keyboard's I2C bus and has no enable pin -- but
// it stops interrupting anything and stops being read, which is what the setting
// buys. The directly-addressed path can only stop being read: this library
// version's BMI270 driver does not implement sleep().
void end();

// Averages about half a second of a still board into the zero-rate offset. Only
// meaningful while nobody is touching it; the card says so on screen. False if
// it could not get enough samples to claim one, which the card reports as FAIL
// rather than as a success nobody checked.
bool calibrate();

// One sample of all three axes in deg/s, with the calibrated offsets removed.
// False when there is nothing to read, in which case the references are
// untouched. All three are handed out because which two are wanted is a
// decision made above this, and because the check screen has to show what the
// part is actually reporting.
bool readRaw(float& gx, float& gy, float& gz);

#ifdef CRAFTCRAFT_GYRO_FAKE
// Emulator only: QEMU does not model the BMI270, so the tour build injects the
// sample a hand would have produced and everything downstream runs for real.
void fakeSet(float gx, float gy, float gz);
#endif

}  // namespace motion
