#include "rtc.h"
#include <Wire.h>

static constexpr uint8_t ADDR = 0x68;
static constexpr uint8_t REG_TIME = 0x00, REG_STATUS = 0x0F, REG_TEMP = 0x11;
static constexpr uint8_t OSF = 0x80;  // Oscillator Stop Flag

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return (v / 10) << 4 | v % 10; }

static bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(buf, n);
  return Wire.endTransmission() == 0;
}

bool rtcBegin(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.beginTransmission(ADDR);
  return Wire.endTransmission() == 0;
}

bool rtcRead(time_t& utc, bool& lost) {
  uint8_t r[7], st;
  if (!readRegs(REG_TIME, r, 7) || !readRegs(REG_STATUS, &st, 1)) return false;
  lost = st & OSF;
  utc = utcToEpoch(2000 + bcd2bin(r[6]), bcd2bin(r[5] & 0x1F), bcd2bin(r[4] & 0x3F),
                   bcd2bin(r[2] & 0x3F), bcd2bin(r[1] & 0x7F), bcd2bin(r[0] & 0x7F));
  return true;
}

bool rtcWrite(time_t utc) {
  struct tm tm;
  gmtime_r(&utc, &tm);
  const uint8_t r[7] = {bin2bcd(tm.tm_sec), bin2bcd(tm.tm_min), bin2bcd(tm.tm_hour), (uint8_t)(tm.tm_wday + 1),
                        bin2bcd(tm.tm_mday), bin2bcd(tm.tm_mon + 1), bin2bcd(tm.tm_year - 100)};
  uint8_t st;
  if (!writeRegs(REG_TIME, r, 7) || !readRegs(REG_STATUS, &st, 1)) return false;
  st &= ~OSF;
  return writeRegs(REG_STATUS, &st, 1);
}

float rtcTemp() {
  uint8_t t[2];
  if (!readRegs(REG_TEMP, t, 2)) return NAN;
  return (int8_t)t[0] + (t[1] >> 6) * 0.25f;
}
