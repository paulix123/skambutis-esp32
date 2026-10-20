// Tvarkaraščio logika – be Arduino, testuojama kompiuteryje (pio test -e native).
// Portas iš skambutis.py bell_times(): kiekvienai pamokai skambutis prieš, pradžioje ir (jei įjungta) pabaigoje.
#pragma once
#include <cstdint>
#include <vector>

struct Lesson { int start, end; };  // minutės nuo vidurnakčio

struct Schedule {
  int preMinutes = 2;
  bool ringEnd = true;
  uint8_t days = 0x1F;  // bitai: 0 = pirmadienis ... 6 = sekmadienis
  std::vector<Lesson> lessons;
};

enum class Bell : uint8_t { None, Pries, Pradzia, Pabaiga };

struct Hit {
  Bell kind = Bell::None;
  int lesson = 0;  // 1..N
};

int parseHHMM(const char* s);  // "08:30" -> 510, bloga reikšmė -> -1
Hit bellAt(const Schedule& s, int weekday, int minute);  // weekday 0 = pirmadienis
int nextBell(const Schedule& s, int weekday, int minute);  // sekantis šiandien po minute, arba -1
