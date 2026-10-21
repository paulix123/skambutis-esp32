// Versijų palyginimas OTA atnaujinimams. Versija – git describe: "v0.3.0", "v0.3.0-2-gabc123-dirty", "2de3dce".
#pragma once
#include <cstdio>

struct Ver {
  int a = 0, b = 0, c = 0;
};

// "v0.3.0..." -> {0,3,0}; ne X.Y.Z (pvz. tik commit hash) -> {0,0,0}, t.y. bet kuris leidimas naujesnis
inline Ver parseVer(const char* s) {
  Ver v;
  if (s && (*s == 'v' || *s == 'V')) s++;
  if (!s || sscanf(s, "%d.%d.%d", &v.a, &v.b, &v.c) != 3) v = Ver();
  return v;
}

inline bool newerVer(const char* candidate, const char* current) {
  Ver x = parseVer(candidate), y = parseVer(current);
  if (x.a != y.a) return x.a > y.a;
  if (x.b != y.b) return x.b > y.b;
  return x.c > y.c;
}
