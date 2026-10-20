// DS3231 laikrodis (I2C 0x68). Laikas saugomas UTC – vasaros/žiemos laiką sutvarko TZ.
#pragma once
#include <ctime>

bool rtcBegin(int sda, int scl);          // ar modulis atsako
bool rtcRead(time_t& utc, bool& lost);    // lost: laikrodis buvo sustojęs (išsekusi baterija / nenustatytas)
bool rtcWrite(time_t utc);                // įrašo laiką ir išvalo "sustojęs" vėliavėlę
float rtcTemp();                          // DS3231 vidinis termometras, °C

// UTC kalendorinė data -> epoch (newlib neturi timegm). Algoritmas: H. Hinnant, days_from_civil.
inline time_t utcToEpoch(int y, int m, int d, int hh, int mm, int ss) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = era * 146097L + (long)doe - 719468;
  return (time_t)days * 86400 + hh * 3600 + mm * 60 + ss;
}
