// pio test -e native: kada OTA laikys leidimą naujesniu
#include <unity.h>
#include "version.h"

void setUp() {}
void tearDown() {}

void test_newer() {
  TEST_ASSERT_TRUE(newerVer("0.3.0", "0.2.1"));
  TEST_ASSERT_TRUE(newerVer("0.10.0", "0.9.9"));    // skaičiai, ne tekstas
  TEST_ASSERT_TRUE(newerVer("1.0.0", "0.99.99"));
  TEST_ASSERT_TRUE(newerVer("0.3.1", "v0.3.0-4-gabc1234-dirty"));
}

void test_not_newer() {
  TEST_ASSERT_FALSE(newerVer("0.3.0", "v0.3.0"));
  TEST_ASSERT_FALSE(newerVer("0.3.0", "v0.3.0-2-gabc1234-dirty"));  // kūrėjo build'as virš leidimo – nesiatnaujina
  TEST_ASSERT_FALSE(newerVer("0.2.9", "0.3.0"));                     // senesnis – niekada
}

void test_unknown_versions() {
  TEST_ASSERT_TRUE(newerVer("0.3.0", "2de3dce"));  // tik hash (be žymų) – leidimas naujesnis
  TEST_ASSERT_TRUE(newerVer("0.3.0", "dev"));
  TEST_ASSERT_FALSE(newerVer("blogas", "0.1.0"));
  TEST_ASSERT_FALSE(newerVer(nullptr, "0.1.0"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_newer);
  RUN_TEST(test_not_newer);
  RUN_TEST(test_unknown_versions);
  return UNITY_END();
}
