// pio test -e native  (assert'ai iš skambutis.py selftest())
#include <unity.h>
#include "schedule.h"

static Schedule one() {
  Schedule s;
  s.lessons = {{parseHHMM("08:00"), parseHHMM("08:45")}};
  return s;
}

void setUp() {}
void tearDown() {}

void test_parse() {
  TEST_ASSERT_EQUAL(510, parseHHMM("08:30"));
  TEST_ASSERT_EQUAL(0, parseHHMM("00:00"));
  TEST_ASSERT_EQUAL(-1, parseHHMM("24:00"));
  TEST_ASSERT_EQUAL(-1, parseHHMM("8.30"));
  TEST_ASSERT_EQUAL(-1, parseHHMM("08:30x"));
}

void test_monday() {
  Schedule s = one();
  Hit h = bellAt(s, 0, parseHHMM("07:58"));
  TEST_ASSERT(h.kind == Bell::Pries);
  TEST_ASSERT_EQUAL(1, h.lesson);
  TEST_ASSERT(bellAt(s, 0, parseHHMM("08:00")).kind == Bell::Pradzia);
  TEST_ASSERT(bellAt(s, 0, parseHHMM("08:45")).kind == Bell::Pabaiga);
  TEST_ASSERT(bellAt(s, 0, parseHHMM("08:01")).kind == Bell::None);
}

void test_sunday_and_ring_end() {
  Schedule s = one();
  TEST_ASSERT(bellAt(s, 6, parseHHMM("08:00")).kind == Bell::None);
  s.ringEnd = false;
  TEST_ASSERT(bellAt(s, 0, parseHHMM("08:45")).kind == Bell::None);
}

void test_wrap_and_zero_pre() {
  Schedule s;
  s.lessons = {{parseHHMM("00:01"), parseHHMM("00:46")}};
  TEST_ASSERT(bellAt(s, 0, parseHHMM("23:59")).kind == Bell::Pries);
  s.preMinutes = 0;  // pries sutampa su pradžia – laimi pradžia
  TEST_ASSERT(bellAt(s, 0, parseHHMM("00:01")).kind == Bell::Pradzia);
}

void test_next() {
  Schedule s = one();
  TEST_ASSERT_EQUAL(parseHHMM("07:58"), nextBell(s, 0, parseHHMM("07:00")));
  TEST_ASSERT_EQUAL(parseHHMM("08:45"), nextBell(s, 0, parseHHMM("08:00")));
  TEST_ASSERT_EQUAL(-1, nextBell(s, 0, parseHHMM("08:45")));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse);
  RUN_TEST(test_monday);
  RUN_TEST(test_sunday_and_ring_end);
  RUN_TEST(test_wrap_and_zero_pre);
  RUN_TEST(test_next);
  return UNITY_END();
}
