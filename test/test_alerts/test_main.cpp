// pio test -e native  (tekstai – tikri lt72.lt pranešimai, 2026-10)
#include <unity.h>
#include "alerts.h"

static const char* RAUDONA =
    "<p>LK / NKVC: ORO PAVOJUS (RAUDONA). Galimai fiksuotas dronas. Nedelsdami skubėkite į saugią vietą.</p>\n<hr>\n"
    "<p>LTU AF/NCMC: AIR RAID WARNING (RED).</p>\n<hr>\n<p><strong>Išsiuntimo data ir laikas: </strong>2026 m. spalio 18 d. "
    "07:09 val.<br /><strong>Siuntimo teritorija: </strong>Šiaulių ir Panevėžio apkritys</p>\n";
static const char* GELTONA =
    "<p>Lietuvos kariuomenė praneša: TIKĖTINAS ORO PAVOJUS (GELTONA). Nusimatykite saugią vietą.</p>\n<hr>\n"
    "<p><strong>Išsiuntimo data ir laikas: </strong>2026 m. spalio 3 d. 09:48 val.<br /><strong>Siuntimo teritorija: "
    "</strong>Vilniaus apskritis</p>\n";
static const char* BALTA =
    "<p>Lietuvos kariuomenė praneša: ORO PAVOJAUS NĖRA (BALTA). Oro pavojus atšaukiamas, tęskite įprastas veiklas.</p>"
    "<p><strong>Siuntimo teritorija: </strong>Panevėžio apskritis</p>\n";
static const char* POLICIJA =
    "<p>Policija informuoja: „Radus įtartinus objektus jų neliesti“.</p>\n<hr>\n<p><strong>Siuntimo teritorija: "
    "</strong>Pasvalio rajonas</p>\n";
static const char* PATIKRINIMAS =
    "<p>PAGD praneša: „Tikrinama gyventojų perspėjimo sistema, įjungiamos sirenos.“</p>"
    "<p><strong>Siuntimo teritorija: </strong>Visa Lietuva</p>\n";

void setUp() {}
void tearDown() {}

void test_level() {
  TEST_ASSERT_EQUAL(2, alertLevel(RAUDONA));
  TEST_ASSERT_EQUAL(1, alertLevel(GELTONA));
  TEST_ASSERT_EQUAL(0, alertLevel(BALTA));
  TEST_ASSERT_EQUAL(-1, alertLevel(POLICIJA));
  TEST_ASSERT_EQUAL(-1, alertLevel(PATIKRINIMAS));
  TEST_ASSERT_EQUAL(1, alertLevel("(GELTONA) po (RAUDONA)"));  // pirmas tekste
}

void test_area() {
  TEST_ASSERT_EQUAL_STRING("Šiaulių ir Panevėžio apkritys", alertArea(RAUDONA).c_str());
  TEST_ASSERT_EQUAL_STRING("Vilniaus apskritis", alertArea(GELTONA).c_str());
  TEST_ASSERT_EQUAL_STRING("Visa Lietuva", alertArea(PATIKRINIMAS).c_str());
  TEST_ASSERT_EQUAL_STRING("", alertArea("<p>be teritorijos</p>").c_str());
}

void test_match_vilnius() {
  const char* v = "Vilniaus";
  TEST_ASSERT_TRUE(areaMatches("Vilniaus apskritis", v));
  TEST_ASSERT_TRUE(areaMatches("Vilniaus miesto savivaldybė", v));
  TEST_ASSERT_TRUE(areaMatches("Vilniaus m. sav.", v));
  TEST_ASSERT_TRUE(areaMatches("Kauno ir Vilniaus apskritys", v));
  TEST_ASSERT_TRUE(areaMatches("Vilniaus rajono ir Vilniaus miesto savivaldybės", v));
  TEST_ASSERT_TRUE(areaMatches("VILNIAUS APSKRITIS", v));
  TEST_ASSERT_TRUE(areaMatches("Visa Lietuva", v));
  TEST_ASSERT_FALSE(areaMatches("Vilniaus rajonas", v));
  TEST_ASSERT_FALSE(areaMatches("Vilniaus r. sav.", v));
  TEST_ASSERT_FALSE(areaMatches("Šiaulių ir Panevėžio apkritys", v));
  TEST_ASSERT_FALSE(areaMatches("Trakų rajonas", v));
  TEST_ASSERT_FALSE(areaMatches("Vilniaus apskritis", ""));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_level);
  RUN_TEST(test_area);
  RUN_TEST(test_match_vilnius);
  return UNITY_END();
}
