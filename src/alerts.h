// Oro pavojaus pranešimai iš lt72.lt (PAGD „Išsiųsti perspėjimo pranešimai“) – teksto analizė be Arduino,
// kad būtų testuojama kompiuteryje (pio test -e native).
#pragma once
#include <string>

// lygis pagal "(BALTA)" / "(GELTONA)" / "(RAUDONA)" tekste: 0, 1, 2 (= SIGNALS indeksas); -1 – ne oro pavojus
int alertLevel(const std::string& html);
// "...Siuntimo teritorija: </strong>Vilniaus apskritis</p>" -> "Vilniaus apskritis"
std::string alertArea(const std::string& html);
// ar teritorija apima vietovę (kilmininku): "Vilniaus" tinka "Vilniaus apskritis", "Vilniaus m. sav.",
// bet ne "Vilniaus rajonas" (kita savivaldybė); "Visa Lietuva" tinka visada
bool areaMatches(const std::string& area, const std::string& place);
