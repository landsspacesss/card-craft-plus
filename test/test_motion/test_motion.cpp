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

// The mapping is the thing the first real device got wrong: yaw was taken from
// the gyro's Z, which on this board is the axis out of the panel -- so turning
// the body like a steering wheel turned the view, and turning about the vertical
// (the thing a player means by "look left") did nothing. These pin the mapping
// table so it cannot drift again.
void test_turn_rates_come_from_the_selected_axis_and_sign(void) {
  raycast::setFovDeg(75);
  const float want = 98.0f * 3.14159265f / 180.0f;   // 100 deg/s less the deadband
  // 0 Y+, 1 Y-, 2 Z+, 3 Z-
  TEST_ASSERT_FLOAT_WITHIN(1e-3f,  want, motion::turnRateRad(0.0f, 100.0f, 0.0f, 0, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -want, motion::turnRateRad(0.0f, 100.0f, 0.0f, 1, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f,  want, motion::turnRateRad(0.0f, 0.0f, 100.0f, 2, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -want, motion::turnRateRad(0.0f, 0.0f, 100.0f, 3, 1));
  // ...and the axis not selected contributes nothing, which is what tells the
  // player the readout on the check screen is the one being used.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::turnRateRad(0.0f, 0.0f, 100.0f, 0, 1));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::turnRateRad(0.0f, 100.0f, 0.0f, 2, 1));
}

void test_tilt_rates_come_from_x_and_can_be_inverted(void) {
  raycast::setFovDeg(75);
  const float want = 98.0f * 3.14159265f / 180.0f * raycast::proj();
  TEST_ASSERT_FLOAT_WITHIN(0.2f,  want, motion::tiltRateRows(100.0f, 0.0f, 0.0f, 0, 1));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, -want, motion::tiltRateRows(100.0f, 0.0f, 0.0f, 1, 1));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::tiltRateRows(0.0f, 100.0f, 100.0f, 0, 1));
}

// A hand on the device produces all three rates at once; the two that matter
// must not steal each other's axis.
void test_both_axes_are_read_from_one_sample(void) {
  raycast::setFovDeg(75);
  const float turn  = motion::turnRateRad(0.0f, -60.0f, 0.0f, 0, 1);
  const float tilt  = motion::tiltRateRows(-60.0f, 0.0f, 0.0f, 0, 1);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -58.0f * 3.14159265f / 180.0f, turn);
  TEST_ASSERT_TRUE(tilt < 0.0f);
}

void test_mapping_names_are_what_the_card_shows(void) {
  TEST_ASSERT_EQUAL_STRING("Y+", motion::turnName(0));
  TEST_ASSERT_EQUAL_STRING("Y-", motion::turnName(1));
  TEST_ASSERT_EQUAL_STRING("Z+", motion::turnName(2));
  TEST_ASSERT_EQUAL_STRING("Z-", motion::turnName(3));
  TEST_ASSERT_EQUAL_STRING("X+", motion::tiltName(0));
  TEST_ASSERT_EQUAL_STRING("X-", motion::tiltName(1));
  // Junk indexes fall back to the default rather than reading off the end.
  TEST_ASSERT_EQUAL_STRING("Y+", motion::turnName(9));
  TEST_ASSERT_EQUAL_STRING("X+", motion::tiltName(9));
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
  RUN_TEST(test_turn_rates_come_from_the_selected_axis_and_sign);
  RUN_TEST(test_tilt_rates_come_from_x_and_can_be_inverted);
  RUN_TEST(test_both_axes_are_read_from_one_sample);
  RUN_TEST(test_mapping_names_are_what_the_card_shows);
  RUN_TEST(test_average_bias_removes_a_constant_offset);
  return UNITY_END();
}
