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

void test_brightness_and_fov_are_quantised(void) {
  settings::Settings s = settings::decode(64, 77, 0, 2);
  TEST_ASSERT_EQUAL_UINT8(60, s.brightnessPct);   // floor to a 10% step
  TEST_ASSERT_EQUAL_UINT8(75, s.fovDeg);          // nearest 5 degrees
}

void test_fov_keeps_squares_square(void) {
  for (uint8_t f = settings::kFovMin; f <= settings::kFovMax; f += settings::kFovStep) {
    const float plane = settings::fovPlaneLen(f);
    const float proj  = settings::fovProj(f);
    // A cube is square on screen when the horizontal scale is VIEW_W/2 / plane,
    // so this product is the panel's half-width and nothing else.
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, plane * proj / 120.0f);
    TEST_ASSERT_TRUE(plane > 0.5f && plane < 1.05f);
  }
  // 75 degrees is a touch wider than the 0.75 the game shipped with, and the
  // projection follows it instead of staying at 160.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.767f, settings::fovPlaneLen(75));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 156.4f, settings::fovProj(75));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_the_shipped_ones);
  RUN_TEST(test_default_brightness_matches_the_old_fixed_160);
  RUN_TEST(test_decode_clamps_garbage_to_something_usable);
  RUN_TEST(test_brightness_and_fov_are_quantised);
  RUN_TEST(test_fov_keeps_squares_square);
  return UNITY_END();
}
