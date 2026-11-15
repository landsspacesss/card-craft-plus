// Host-side tests for the settings values.
//
// Nothing here touches the panel or NVS: this is the arithmetic that decides
// what the panel is asked for, and what a card that has never been written --
// or was written by a build that stored different units -- is turned into
// before anyone acts on it.
//
// Run with:  pio test -e native
#include <math.h>
#include <unity.h>

#include "settings.h"

void test_defaults_are_the_shipped_ones(void) {
  settings::Settings s;
  TEST_ASSERT_EQUAL_UINT8(60, s.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(75, s.fovDeg);
  TEST_ASSERT_FALSE(s.gyroOn);
  TEST_ASSERT_EQUAL_UINT8(1, s.gyroSens);   // MED
}

void test_default_brightness_matches_the_old_fixed_160(void) {
  settings::Settings s;
  TEST_ASSERT_EQUAL_UINT8(153, settings::brightnessDuty(s));   // 60*255/100
}

// A card that has run an older build, or lost power in the middle of a write,
// hands back whatever is in NVS. None of it may reach the panel unclamped: a
// brightness of 0 is a black screen with no way back, and a FOV of 0 divides
// the projection by zero.
void test_decode_clamps_garbage_to_something_usable(void) {
  settings::Settings low = settings::decode(0, 255, 1, 9);
  TEST_ASSERT_EQUAL_UINT8(10, low.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(90, low.fovDeg);
  TEST_ASSERT_EQUAL_UINT8(1, low.gyroSens);
  TEST_ASSERT_TRUE(low.gyroOn);

  settings::Settings high = settings::decode(255, 0, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(100, high.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(60, high.fovDeg);
  TEST_ASSERT_FALSE(high.gyroOn);
}

// The two mapping bytes have defaults and are clamped like everything else.
void test_gyro_mapping_defaults_and_survives_garbage(void) {
  settings::Settings s;
  // The axis is Y and the direction is negative: the device reported turning
  // left moving the view right, so the default is the corrected pair and not
  // the first guess.
  TEST_ASSERT_EQUAL_UINT8(settings::kTurnYMinus, s.gyroTurn);
  TEST_ASSERT_EQUAL_UINT8(settings::kTiltXPlus, s.gyroTilt);
  const settings::Settings junk = settings::decode(60, 75, 1, 1, 200, 99);
  TEST_ASSERT_EQUAL_UINT8(settings::kTurnDefault, junk.gyroTurn);
  TEST_ASSERT_EQUAL_UINT8(settings::kTiltDefault, junk.gyroTilt);
  const settings::Settings kept = settings::decode(60, 75, 1, 1, 3, 1);
  TEST_ASSERT_EQUAL_UINT8(3, kept.gyroTurn);
  TEST_ASSERT_EQUAL_UINT8(1, kept.gyroTilt);
}

void test_brightness_and_fov_are_quantised(void) {
  settings::Settings s = settings::decode(64, 77, 0, 2);
  TEST_ASSERT_EQUAL_UINT8(60, s.brightnessPct);   // floor to a 10% step
  TEST_ASSERT_EQUAL_UINT8(75, s.fovDeg);          // nearest 5 degrees
}

// Review Focus 5: the lowest setting still has to be a lit panel, because the
// card that would fix it is drawn on that panel. The floor lives in sanitize(),
// so this pins the two together -- a floor with a mapping that reaches zero is
// the same black screen with more steps.
void test_the_lowest_brightness_is_still_a_lit_panel(void) {
  settings::Settings s;
  s.brightnessPct = 0;                    // what a bad write could leave behind
  settings::sanitize(s);
  TEST_ASSERT_EQUAL_UINT8(settings::kBrightnessMin, s.brightnessPct);
  TEST_ASSERT_EQUAL_UINT8(25, settings::brightnessDuty(s));   // 10 * 255 / 100
  TEST_ASSERT_TRUE(settings::brightnessDuty(s) > 0);

  s.brightnessPct = 100;
  TEST_ASSERT_EQUAL_UINT8(255, settings::brightnessDuty(s));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_the_shipped_ones);
  RUN_TEST(test_default_brightness_matches_the_old_fixed_160);
  RUN_TEST(test_decode_clamps_garbage_to_something_usable);
  RUN_TEST(test_brightness_and_fov_are_quantised);
  RUN_TEST(test_gyro_mapping_defaults_and_survives_garbage);
  RUN_TEST(test_the_lowest_brightness_is_still_a_lit_panel);
  return UNITY_END();
}
