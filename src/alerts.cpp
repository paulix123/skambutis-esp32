#include "alerts.h"

static std::string lower(std::string s) {  // tik ASCII – lietuviškos raidės lieka, vietovių pradžios ASCII
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return s;
}

int alertLevel(const std::string& html) {
  static const char* MARK[] = {"(BALTA)", "(GELTONA)", "(RAUDONA)"};
  int best = -1;
  size_t at = std::string::npos;
  for (int i = 0; i < 3; i++) {  // keli – imam pirmą tekste (pranešimo antraštė)
    size_t p = html.find(MARK[i]);
    if (p < at) at = p, best = i;
  }
  return best;
}

std::string alertArea(const std::string& html) {
  const std::string key = "Siuntimo teritorija:";
  size_t p = html.find(key);
  if (p == std::string::npos) return "";
  std::string out;
  bool tag = false;
  for (p += key.size(); p < html.size(); p++) {
    char c = html[p];
    if (c == '<') {
      if (out.find_first_not_of(' ') != std::string::npos) break;  // teksto pabaiga
      tag = true;
    } else if (c == '>') {
      tag = false;
    } else if (!tag && c != '\n') {
      out += c;
    }
  }
  size_t a = out.find_first_not_of(' '), b = out.find_last_not_of(' ');
  return a == std::string::npos ? "" : out.substr(a, b - a + 1);
}

bool areaMatches(const std::string& area, const std::string& place) {
  std::string s = lower(area), w = lower(place);
  if (w.empty()) return false;
  if (s.find("lietuv") != std::string::npos) return true;
  for (size_t p = s.find(w); p != std::string::npos; p = s.find(w, p + 1)) {
    std::string next = s.substr(p + w.size(), 4);
    if (next.rfind(" raj", 0) == 0 || next.rfind(" r.", 0) == 0) continue;  // "Vilniaus rajonas", "Vilniaus r. sav."
    return true;
  }
  return false;
}
