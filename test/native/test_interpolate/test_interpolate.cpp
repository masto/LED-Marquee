#include <gtest/gtest.h>
#include <interpolate.h>

#include <string>

using namespace std::string_literals;

TEST(InterpolateTest, CopiesUninterpolatedStrings) {
  EXPECT_EQ(led_marquee::Interpolate("foo bar"), "foo bar");
}

TEST(InterpolateTest, DoesNotInterpolateJunk) {
  std::string a = "this is {ff0000}red";
  EXPECT_EQ(led_marquee::Interpolate(a), a);

  a = "this is {#ff000}red";
  EXPECT_EQ(led_marquee::Interpolate(a), a);

  a = "this is {ff0000}red";
  EXPECT_EQ(led_marquee::Interpolate(a), a);

  a = "this is {#ff0000 }red";
  EXPECT_EQ(led_marquee::Interpolate(a), a);

  a = "this is {{ff0000}red";
  EXPECT_EQ(led_marquee::Interpolate(a), a);

  a = "this is {#fg0000}red";
  EXPECT_EQ(led_marquee::Interpolate(a), a);
}

TEST(InterpolateTest, InterpolatesRgb) {
  std::string a = "this is {#ff0000}red";
  EXPECT_EQ(led_marquee::Interpolate(a), "this is \xe0\xff\x00\x00red"s);

  a = "this is {#ff0000}red{#00ff00}green{#0000ff}blue";
  EXPECT_EQ(
      led_marquee::Interpolate(a),
      "this is \xe0\xff\x00\x00red\xe0\x00\xff\x00green\xe0\x00\x00\x{ff}blue"s);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // if you plan to use GMock, replace the line above with
  // ::testing::InitGoogleMock(&argc, argv);

  if (RUN_ALL_TESTS())
    ;

  // Always return zero-code and allow PlatformIO to parse results
  return 0;
}
