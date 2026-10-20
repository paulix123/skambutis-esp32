// pio test -e native: utcToEpoch turi sutapti su sistemos timegm visam DS3231 diapazonui (2000–2099)
#include <unity.h>
#include <ctime>
#include "rtc.h"

void setUp() {}
void tearDown() {}

void test_known() {
  TEST_ASSERT_EQUAL_INT64(946684800, utcToEpoch(2000, 1, 1, 0, 0, 0));
  TEST_ASSERT_EQUAL_INT64(1709164800, utcToEpoch(2024, 2, 29, 0, 0, 0));  // keliamieji metai
  TEST_ASSERT_EQUAL_INT64(1792411200, utcToEpoch(2026, 10, 19, 12, 0, 0));
}

void test_roundtrip_with_timegm() {
  for (time_t t = 946684800; t < 4102444800; t += 86400 * 7 + 3661) {  // kas savaitę + 1h1m1s
    struct tm tm;
    gmtime_r(&t, &tm);
    TEST_ASSERT_EQUAL_INT64(t, utcToEpoch(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known);
  RUN_TEST(test_roundtrip_with_timegm);
  return UNITY_END();
}
