// Host-side tests for the gyro arithmetic: deadband, sensitivity, the two unit
// conversions, and the bias a calibration leaves behind.
//
// The sensor itself cannot be here -- QEMU does not model the BMI270 and the
// real one is on a board -- so everything that decides what a rate MEANS is
// kept out of the driver and tested here.
//
// Run with:  pio test -e native
#include <math.h>
#include <unity.h>

#include "motion.h"
#include "raycast.h"

void test_deadband_swallows_resting_noise_and_keeps_the_rest(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::deadbandDps(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::deadbandDps(2.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::deadbandDps(-2.0f));
  // Inside the band the result is zero but the edge is not a cliff: 2.5 deg/s
  // has to be a gentle movement, not nothing and not the whole 2.5.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, motion::deadbandDps(2.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, motion::deadbandDps(-2.5f));
}

void test_sensitivity_levels_are_the_documented_ratios(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.5f, motion::sensFactor(0));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::sensFactor(1));
  TEST_ASSERT_EQUAL_FLOAT(1.8f, motion::sensFactor(2));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::sensFactor(9));   // junk level: MED
}

void test_rates_convert_to_the_units_the_simulation_wants(void) {
  raycast::setFovDeg(75);
  // 100 deg/s at MED is 1:1 with the hand. Two of the hundred are the deadband,
  // so 98 deg/s is what reaches the camera: 1.710 rad/s.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 98.0f * 3.14159265f / 180.0f, motion::yawRateRad(100.0f, 1));
  // ...and pitch moves the horizon by proj() rows per radian, the same
  // projection the renderer uses, so it stays proportional at any FOV.
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 98.0f * 3.14159265f / 180.0f * raycast::proj(),
                           motion::pitchRateRows(100.0f, 1));
  // Deadbanded input stays dead, whatever the sensitivity.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::yawRateRad(1.5f, 2));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::pitchRateRows(-1.0f, 2));
  // Sensitivity scales the same rate differently.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 98.0f * 3.14159265f / 180.0f * 1.8f,
                           motion::yawRateRad(100.0f, 2));
  raycast::setFovDeg(75);
}

// A stationary sensor reads a constant offset, not zero. Averaging a second of
// it and subtracting is the whole calibration; what matters is that what is
// left is inside the deadband, so the view does not creep while it sits still.
void test_average_bias_removes_a_constant_offset(void) {
  const float still[] = { 1.4f, 1.2f, 1.6f, 1.3f, 1.5f };
  const float bias = motion::averageBias(still, 5);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.4f, bias);
  for (int i = 0; i < 5; ++i)
    TEST_ASSERT_TRUE(fabsf(motion::applyBias(still[i], bias)) <= motion::DEADBAND_DPS);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::averageBias(still, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_deadband_swallows_resting_noise_and_keeps_the_rest);
  RUN_TEST(test_sensitivity_levels_are_the_documented_ratios);
  RUN_TEST(test_rates_convert_to_the_units_the_simulation_wants);
  RUN_TEST(test_average_bias_removes_a_constant_offset);
  return UNITY_END();
}
