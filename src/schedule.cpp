#include "schedule.h"
#include <cstdio>

int parseHHMM(const char* s) {
  if (!s) return -1;
  int h, m;
  char extra;
  if (sscanf(s, "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

Hit bellAt(const Schedule& s, int weekday, int minute) {
  Hit hit;
  if (!(s.days >> weekday & 1)) return hit;
  // kaip skambutis.py: einam pamokomis iš eilės, sutampančią minutę perrašo vėlesnis įrašas
  for (size_t i = 0; i < s.lessons.size(); i++) {
    const Lesson& l = s.lessons[i];
    int pre = ((l.start - s.preMinutes) % 1440 + 1440) % 1440;
    int n = (int)i + 1;
    if (pre == minute) hit = {Bell::Pries, n};
    if (l.start == minute) hit = {Bell::Pradzia, n};
    if (s.ringEnd && l.end == minute) hit = {Bell::Pabaiga, n};
  }
  return hit;
}

int nextBell(const Schedule& s, int weekday, int minute) {
  for (int m = minute + 1; m < 1440; m++)
    if (bellAt(s, weekday, m).kind != Bell::None) return m;
  return -1;
}
