// Mokyklos skambutis: groja pagal tvarkaraštį iš /config.json (LittleFS); jei failo nėra – numatytasis.
// Konfigūruojama per HTTP API vietiniame tinkle (žr. API skyrių žemiau), PIN – antraštė X-Pin.
// Serial: p - bandomasis skambutis, r - bandomasis "prieš", s - stop, i - būsena (ir PIN),
//         x - testinė pamoka po 3 min (tik RAM), w - pamiršti WiFi ir perkrauti, t - I2S laidų testas, n - WiFi tinklai,
//         c - laikrodis (DS3231), T<unix laikas>\n - nustatyti laiką be interneto (sistemos ir DS3231), u - atnaujinimas,
//         b - būsenos eilutė, H - watchdog testas (užstringa tyčia),
//         A<id>\n - perspėjimai: laikyti matytais iki lt72.lt įrašo <id> (bandymui – vėl apdoroti naujesnius)
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <algorithm>
#include <vector>
#include "Audio.h"
#include "alerts.h"
#include "rtc.h"
#include "schedule.h"
#include "version.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"  // tikrą įrašo tools/version.py (git describe)
#endif
// OTA: GitHub leidimas (žyma v*) – firmware.json {version, url, sha256, size} + firmware.bin
constexpr const char* UPDATE_URL = "https://github.com/paulix123/skambutis-esp32/releases/latest/download/firmware.json";
// PCM5102A: BCK, LCK, DIN. SCK -> GND
constexpr uint8_t I2S_BCK = 4, I2S_LCK = 5, I2S_DIN = 6;
// DS3231: SDA, SCL; VCC -> 3V3 (ne 5 V)
constexpr uint8_t RTC_SDA = 8, RTC_SCL = 18;  // SCL perkeltas nuo 9 (neveikė kontaktas/laidas)
constexpr const char* TZ_VILNIUS = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr const char* HOSTNAME = "skambutis-esp";  // "skambutis.local" mokyklos tinkle jau užimtas
constexpr const char* AP_NAME = "Skambutis-setup";
// ponytail: AP slaptažodis kode (repo viešas); portalas atsiranda tik be WiFi, API saugo atskiras PIN
constexpr const char* AP_PASS = "skambutis";
const char* SIGNALS[] = {"BALTAS", "GELTONAS", "RAUDONAS"};
// Oro pavojaus pranešimai: lt72.lt (PAGD) kategorija 49 „Išsiųsti perspėjimo pranešimai“. RSS Cloudflare laiko iki 4 h,
// todėl WordPress REST + unikalus parametras (&t=) – visada šviežia. Lygis BALTA/GELTONA/RAUDONA = SIGNALS indeksas.
constexpr const char* ALERTS_URL = "https://lt72.lt/wp-json/wp/v2/posts?categories=49&per_page=10&_fields=id,date_gmt,content";
const char* LEVELS[] = {"BALTA", "GELTONA", "RAUDONA"};

// tas pats, kas skambutis.py DEFAULT: 8 pamokos po 45 min nuo 08:30.
// sounds[].use: bendras (pradžia/pabaiga) | pries | BALTAS | GELTONAS | RAUDONAS | off
const char* DEFAULT_CONFIG = R"({
  "pre_minutes": 2, "lead_ms": 200, "ring_end": true, "volume": 21, "days": [0, 1, 2, 3, 4],
  "alerts": "off", "alert_place": "Vilniaus",
  "lessons": [
    {"start": "08:30", "end": "09:15"}, {"start": "09:25", "end": "10:10"},
    {"start": "10:20", "end": "11:05"}, {"start": "11:15", "end": "12:00"},
    {"start": "12:20", "end": "13:05"}, {"start": "13:25", "end": "14:10"},
    {"start": "14:20", "end": "15:05"}, {"start": "15:15", "end": "16:00"}],
  "sounds": [
    {"file": "/skambutis.mp3", "name": "Skambutis", "use": "bendras"},
    {"file": "/pries.mp3", "name": "Prieš pamoką", "use": "pries"}]
})";

Audio audio;
WiFiManager wm;
WebServer server(80);
Preferences prefs;
Schedule sched;
std::vector<String> poolPries, poolBendras;
String signalFile[3];
String pin;
int leadMs = 200;
bool rtcOk = false, webStarted = false;
volatile bool ntpSynced = false;  // nustato SNTP gija, apdoroja loop()
bool updateRequested = false;
String updateInfo = "dar netikrinta";
String alertsMode = "off", alertPlace = "Vilniaus";  // alerts: off | log (tik žurnalas) | play (groti signalą)
String alertInfo = "dar netikrinta", alertLast;
uint32_t alertId = 0;  // paskutinis matytas lt72.lt įrašas (NVS) – senesni niekada negrojami
bool fsOk = false;                // žurnalas į failą tik kai LittleFS paruoštas
constexpr const char* LOG_FILE = "/log.txt";
constexpr const char* LOG_OLD = "/log.old";
constexpr size_t LOG_MAX = 64 * 1024;  // tiek pasiekus – log.txt -> log.old (iš viso ~128 KB, ~2–3 savaitės)

void logf(const char* fmt, ...) {
  char msg[160], ts[24] = "--:--:--";
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  time_t now = time(nullptr);
  struct tm tm;
  if (now > 1700000000) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime_r(&now, &tm));
  Serial.printf("%s %s\n", ts, msg);
  if (!fsOk) return;
  File f = LittleFS.open(LOG_FILE, "a");  // išlieka po perkrovimo / dingus elektrai
  if (!f) return;
  f.printf("%s %s\n", ts, msg);
  bool full = f.size() > LOG_MAX;
  f.close();
  if (full) {
    LittleFS.remove(LOG_OLD);
    LittleFS.rename(LOG_FILE, LOG_OLD);
  }
}

// --- konfigūracija ----------------------------------------------------------

int signalIndex(const String& s) {
  for (int i = 0; i < 3; i++)
    if (s == SIGNALS[i]) return i;
  return -1;
}

// Patikrina VISKĄ ir tik tada pritaiko – klaidingas config nieko nesugadina.
bool applyConfig(JsonDocument& doc, String& err) {
  int pre = doc["pre_minutes"] | 2, lead = doc["lead_ms"] | 200, vol = doc["volume"] | 21;
  if (pre < 0 || pre > 30) { err = "pre_minutes turi būti 0–30"; return false; }
  if (lead < 0 || lead > 5000) { err = "lead_ms turi būti 0–5000"; return false; }
  if (vol < 0 || vol > 21) { err = "volume turi būti 0–21"; return false; }
  String am = doc["alerts"] | "off", ap = doc["alert_place"] | "Vilniaus";
  ap.trim();
  if (am != "off" && am != "log" && am != "play") { err = "alerts: off | log | play"; return false; }
  if (ap.length() < 3 || ap.length() > 40) { err = "alert_place: vietovė kilmininku, pvz. Vilniaus"; return false; }
  Schedule s;
  s.preMinutes = pre;
  s.ringEnd = doc["ring_end"] | true;
  s.days = 0;
  for (JsonVariantConst d : doc["days"].as<JsonArrayConst>()) {
    int v = d | -1;
    if (v < 0 || v > 6) { err = "days: 0 (pirmadienis) – 6 (sekmadienis)"; return false; }
    s.days |= 1 << v;
  }
  for (JsonObjectConst l : doc["lessons"].as<JsonArrayConst>()) {
    const char *a = l["start"] | "", *b = l["end"] | "";
    int x = parseHHMM(a), y = parseHHMM(b);
    if (x < 0 || y < 0) { err = String("bloga pamoka ") + a + "–" + b; return false; }
    s.lessons.push_back({x, y});
  }
  std::sort(s.lessons.begin(), s.lessons.end(), [](const Lesson& p, const Lesson& q) { return p.start < q.start; });
  std::vector<String> pp, pb;
  String sig[3];
  for (JsonObjectConst snd : doc["sounds"].as<JsonArrayConst>()) {
    String f = snd["file"] | "", use = snd["use"] | "off";
    if (!LittleFS.exists(f)) { err = "nėra failo " + f; return false; }
    if (use == "bendras") pb.push_back(f);
    else if (use == "pries") pp.push_back(f);
    else if (use != "off") {
      int i = signalIndex(use);
      if (i < 0) { err = "blogas use: " + use; return false; }
      sig[i] = f;
    }
  }
  sched = s;
  leadMs = lead;
  alertsMode = am;
  alertPlace = ap;
  audio.setVolume(vol);
  poolPries = pp;
  poolBendras = pb;
  for (int i = 0; i < 3; i++) signalFile[i] = sig[i];
  return true;
}

void loadConfig() {
  JsonDocument doc;
  String err;
  File f = LittleFS.open("/config.json");
  bool parsed = f && !deserializeJson(doc, f);
  if (f) f.close();
  if (parsed && applyConfig(doc, err)) {
    logf("config: /config.json, %u pamokų, prieš %d min", sched.lessons.size(), sched.preMinutes);
    return;
  }
  if (f) logf("KLAIDA: config.json (%s) – naudojamas numatytasis", parsed ? err.c_str() : "neteisingas JSON");
  deserializeJson(doc, DEFAULT_CONFIG);
  if (!applyConfig(doc, err)) logf("KLAIDA: numatytasis config: %s", err.c_str());
  else logf("config: numatytasis, %u pamokų, prieš %d min", sched.lessons.size(), sched.preMinutes);
}

// ištrina įkeltus garsus (/s/), kurių config nebenaudoja, ir nebaigtus įkėlimus
void removeUnusedSounds(JsonDocument& doc) {
  std::vector<String> del;
  File dir = LittleFS.open("/s");
  for (File e = dir ? dir.openNextFile() : File(); e; e = dir.openNextFile()) {
    String p = e.path();
    e.close();
    bool used = false;
    for (JsonObjectConst snd : doc["sounds"].as<JsonArrayConst>()) used |= p == (snd["file"] | "");
    if (!used) del.push_back(p);
  }
  for (const String& p : del) {
    LittleFS.remove(p);
    logf("ištrintas nenaudojamas %s", p.c_str());
  }
}

// --- grojimas ir tvarkaraštis -------------------------------------------------

bool signalOn = false;  // groja civilinės saugos signalas – tvarkaraščio skambutis jo nenutraukia

void playFile(const String& f, const char* label, bool signal = false) {
  logf("%s: groja %s", label, f.c_str());
  signalOn = signal;
  audio.connecttoFS(LittleFS, f.c_str());
}

void ring(bool pries, const char* label) {
  // tuščias "prieš" fondas -> skamba tas pats, kas pradžiai
  const auto& pool = pries && !poolPries.empty() ? poolPries : poolBendras;
  if (pool.empty()) { logf("%s: nėra garso takelių", label); return; }
  playFile(pool[esp_random() % pool.size()], label);
}

// localtime + lead_ms: grotuvo startas užtrunka, tad minutę "pamatom" anksčiau
bool nowLocal(struct tm& tm) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000) return false;  // laikas dar nesinchronizuotas
  time_t t = ((int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 + leadMs) / 1000;
  localtime_r(&t, &tm);
  return true;
}

void checkBell() {
  static int lastMin = -1;
  struct tm tm;
  if (!nowLocal(tm)) return;
  int minute = tm.tm_hour * 60 + tm.tm_min;
  if (minute == lastMin) return;
  // po starto/sinchronizacijos neskambinam, jei minutė jau seniai prasidėjo
  bool late = lastMin == -1 && tm.tm_sec > 5;
  lastMin = minute;
  if (late) return;
  Hit h = bellAt(sched, (tm.tm_wday + 6) % 7, minute);
  char label[32];
  switch (h.kind) {
    case Bell::None: return;
    case Bell::Pries: snprintf(label, sizeof label, "%d pam. -%d min", h.lesson, sched.preMinutes); break;
    case Bell::Pradzia: snprintf(label, sizeof label, "%d pam. pradžia", h.lesson); break;
    case Bell::Pabaiga: snprintf(label, sizeof label, "%d pam. pabaiga", h.lesson); break;
  }
  if (signalOn && audio.isRunning()) { logf("%s: praleista – groja signalas", label); return; }
  ring(h.kind == Bell::Pries, label);
}

// --- atnaujinimai (OTA) -------------------------------------------------------
// Nauja versija paleidžiama "nepatvirtinta": jei nulūžta prieš patvirtinimą arba per 10 min neprisijungia
// prie WiFi – bootloader'is grįžta prie ankstesnės. Patvirtinam patys (žr. loop), todėl branduolio
// silpną C funkciją pakeičiam savo – būtinai extern "C", kitaip C++ vardas nesutampa ir lieka numatytoji
extern "C" bool verifyRollbackLater() { return true; }

bool otaPendingVerify() {
  esp_ota_img_states_t st;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
}

bool httpsBegin(HTTPClient& http, NetworkClientSecure& client, const String& url) {
  client.useBuiltinCACertBundle();  // tikrinam, kad tikrai GitHub
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // releases/... -> release-assets.githubusercontent.com
  http.setTimeout(15000);
  return http.begin(client, url);
}

// atsisiunčia ir įrašo į kitą programos vietą; SHA-256 tikrinamas prieš patvirtinant
bool otaInstall(const String& url, size_t size, String sha) {
  NetworkClientSecure client;
  HTTPClient http;
  if (!httpsBegin(http, client, url)) { updateInfo = "klaida: netinkamas adresas"; return false; }
  int code = http.GET();
  if (code != 200) { updateInfo = "klaida: firmware.bin HTTP " + String(code); return false; }
  int len = http.getSize();
  if (len <= 0 || (size_t)len != size) { updateInfo = "klaida: blogas dydis " + String(len); return false; }
  if (!Update.begin(len)) { updateInfo = String("klaida: ") + Update.errorString(); return false; }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  auto* stream = http.getStreamPtr();
  static uint8_t buf[4096];
  size_t done = 0;
  uint32_t last = millis();
  while (done < (size_t)len) {
    size_t n = stream->available();
    if (n) {
      n = stream->readBytes(buf, std::min(n, sizeof buf));
      mbedtls_sha256_update(&ctx, buf, n);
      if (Update.write(buf, n) != n) break;
      esp_task_wdt_reset();
      done += n;
      last = millis();
    } else if (millis() - last > 15000) {
      break;
    } else {
      delay(1);
    }
  }
  uint8_t h[32];
  mbedtls_sha256_finish(&ctx, h);
  mbedtls_sha256_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
  sha.toLowerCase();
  if (done != (size_t)len || sha != hex) {
    Update.abort();
    updateInfo = done != (size_t)len ? "klaida: atsisiuntimas nutrūko" : "klaida: SHA-256 nesutampa";
    return false;
  }
  if (!Update.end(true)) { updateInfo = String("klaida: ") + Update.errorString(); return false; }
  return true;
}

// grąžina false, jei reikia pakartoti greičiau (skambutis netrukus / groja)
bool checkUpdate() {
  struct tm tm;
  if (audio.isRunning()) { updateInfo = "atidėta – groja"; return false; }
  if (nowLocal(tm)) {
    int m = tm.tm_hour * 60 + tm.tm_min, nb = nextBell(sched, (tm.tm_wday + 6) % 7, m);
    if (nb >= 0 && nb - m < 10) { updateInfo = "atidėta – netrukus skambutis"; return false; }
  }
  NetworkClientSecure client;
  HTTPClient http;
  if (!httpsBegin(http, client, UPDATE_URL)) { updateInfo = "klaida: netinkamas adresas"; return true; }
  int code = http.GET();
  if (code == 404) { updateInfo = "leidimų dar nėra"; return true; }
  if (code != 200) { updateInfo = "klaida: HTTP " + String(code); return true; }
  JsonDocument d;
  if (deserializeJson(d, http.getString())) { updateInfo = "klaida: firmware.json"; return true; }
  http.end();
  const char* ver = d["version"] | "";
  if (!newerVer(ver, FW_VERSION)) { updateInfo = String("naujausia (") + ver + ")"; return true; }
  logf("atnaujinimas: %s -> %s, siunčiama...", FW_VERSION, ver);
  if (!otaInstall(d["url"] | "", d["size"] | 0, d["sha256"] | "")) {
    logf("atnaujinimas nepavyko: %s", updateInfo.c_str());
    return true;
  }
  logf("atnaujinimas %s įrašytas – perkraunama", ver);
  delay(500);
  ESP.restart();
  return true;
}

// --- PIN --------------------------------------------------------------------

bool validPin(const String& p) {
  if (p.length() < 4 || p.length() > 12) return false;
  for (char c : p)
    if (!isDigit(c)) return false;
  return true;
}

void savePin(const String& p) {
  pin = p;
  prefs.putString("pin", p);
  logf("PIN pakeistas");
}

WiFiManagerParameter* pinParam;  // laukelis WiFi nustatymų puslapyje

// --- HTTP API ---------------------------------------------------------------
//  GET  /status                         be PIN: versija, laikas, sekantis skambutis, WiFi, RTC, vieta
//  GET  /config      POST /config       visas config JSON; POST patikrina, išsaugo, pritaiko, išvalo nenaudojamus garsus
//  GET  /files                          garso failai ir dydžiai
//  GET  /file?f=/s/x.mp3                atsisiųsti garsą
//  POST /upload (multipart "file", .mp3) -> {"file": "/s/xxxxxxxx.mp3"}; į config įtraukti atskirai
//  POST /play?what=bendras|pries|BALTAS|GELTONAS|RAUDONAS  arba  /play?file=/s/x.mp3
//  POST /stop        GET /log           POST /pin {"pin": "1234"}
//  POST /update                         patikrinti/įdiegti atnaujinimą dabar (rezultatas – /status "update")
// ponytail: sinchroninis WebServer – įkėlimo metu (kelios s) loop() stovi, skambutis gali pavėluoti tiek pat;
//           jei trukdys – audio į atskirą FreeRTOS užduotį arba AsyncWebServer.

void sendJson(int code, JsonDocument& d) {
  String out;
  serializeJson(d, out);
  server.send(code, "application/json; charset=utf-8", out);
}

void sendError(int code, const String& msg) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(code, d);
}

bool authed() {
  if (server.header("X-Pin") == pin) return true;
  delay(500);  // lėtina PIN spėliojimą
  sendError(401, "neteisingas PIN");
  return false;
}

void handleStatus() {
  JsonDocument d;
  d["version"] = FW_VERSION;
  d["uptime_s"] = millis() / 1000;
  struct tm tm;
  bool synced = nowLocal(tm);
  d["time_ok"] = synced;
  if (synced) {
    char b[24];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm);
    d["time"] = b;
    int nb = nextBell(sched, (tm.tm_wday + 6) % 7, tm.tm_hour * 60 + tm.tm_min);
    if (nb >= 0) {
      snprintf(b, sizeof b, "%02d:%02d", nb / 60, nb % 60);
      d["next_bell"] = b;
    }
  }
  d["wifi"]["ssid"] = WiFi.SSID();
  d["wifi"]["rssi"] = WiFi.RSSI();
  d["wifi"]["ip"] = WiFi.localIP().toString();
  d["rtc"]["ok"] = rtcOk;
  time_t t;
  bool lost;
  if (rtcOk && rtcRead(t, lost)) {
    d["rtc"]["lost"] = lost;
    d["rtc"]["temp"] = rtcTemp();
  }
  d["fs"]["used"] = LittleFS.usedBytes();
  d["fs"]["total"] = LittleFS.totalBytes();
  d["playing"] = audio.isRunning();
  d["update"] = updateInfo;
  d["alerts"]["mode"] = alertsMode;
  d["alerts"]["info"] = alertsMode == "off" ? "išjungta" : alertInfo;
  d["alerts"]["last"] = alertLast;
  sendJson(200, d);
}

void handleGetConfig() {
  if (!authed()) return;
  File f = LittleFS.open("/config.json");
  if (f) server.streamFile(f, "application/json; charset=utf-8");
  else server.send(200, "application/json; charset=utf-8", DEFAULT_CONFIG);
}

void handlePostConfig() {
  if (!authed()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "neteisingas JSON");
  String err;
  if (!applyConfig(doc, err)) return sendError(400, err);
  File f = LittleFS.open("/config.tmp", "w");
  bool ok = f && serializeJsonPretty(doc, f) > 0;
  if (f) f.close();
  if (!ok || !LittleFS.rename("/config.tmp", "/config.json")) return sendError(500, "nepavyko išsaugoti");
  removeUnusedSounds(doc);
  logf("config išsaugotas per API: %u pamokų", sched.lessons.size());
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void handleFiles() {
  if (!authed()) return;
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (const char* dirName : {"/", "/s"}) {
    File dir = LittleFS.open(dirName);
    for (File e = dir ? dir.openNextFile() : File(); e; e = dir.openNextFile()) {
      String p = e.path();
      if (!e.isDirectory() && (p.endsWith(".mp3") || p.endsWith(".wav"))) {
        JsonObject o = arr.add<JsonObject>();
        o["file"] = p;
        o["size"] = e.size();
      }
      e.close();
    }
  }
  sendJson(200, d);
}

void handleGetFile() {
  if (!authed()) return;
  File f = LittleFS.open(server.arg("f"));
  if (!f || f.isDirectory()) return sendError(404, "nėra failo");
  server.streamFile(f, "audio/mpeg");
}

String upPath, upErr;
File upFile;

void handleUploadChunk() {
  esp_task_wdt_reset();
  HTTPUpload& u = server.upload();
  switch (u.status) {
    case UPLOAD_FILE_START: {
      upPath = "";
      upErr = "";
      if (server.header("X-Pin") != pin) { upErr = "neteisingas PIN"; return; }
      String fn = u.filename;
      fn.toLowerCase();
      if (!fn.endsWith(".mp3")) { upErr = "priimami tik .mp3"; return; }
      size_t len = server.header("Content-Length").toInt();
      if (len + 65536 > LittleFS.totalBytes() - LittleFS.usedBytes()) { upErr = "nepakanka vietos"; return; }
      LittleFS.mkdir("/s");
      char id[9];
      snprintf(id, sizeof id, "%08lx", (unsigned long)esp_random());
      upPath = String("/s/") + id + ".mp3";
      upFile = LittleFS.open(upPath + ".tmp", "w");
      if (!upFile) upErr = "nepavyko sukurti failo";
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (upErr.isEmpty() && upFile.write(u.buf, u.currentSize) != u.currentSize) upErr = "rašymo klaida (vieta?)";
      break;
    case UPLOAD_FILE_END:
      if (upFile) upFile.close();
      if (upErr.isEmpty() && upPath.length()) LittleFS.rename(upPath + ".tmp", upPath);
      else if (upPath.length()) LittleFS.remove(upPath + ".tmp");
      break;
    case UPLOAD_FILE_ABORTED:
      if (upFile) upFile.close();
      if (upPath.length()) LittleFS.remove(upPath + ".tmp");
      upErr = "įkėlimas nutrauktas";
      break;
  }
}

void handleUploadDone() {
  if (upErr == "neteisingas PIN") { delay(500); return sendError(401, upErr); }
  if (upErr.length()) return sendError(400, upErr);
  if (upPath.isEmpty()) return sendError(400, "nėra failo (multipart laukas \"file\")");
  logf("įkeltas %s", upPath.c_str());
  JsonDocument d;
  d["file"] = upPath;
  d["size"] = LittleFS.open(upPath).size();
  sendJson(200, d);
}

void handlePlay() {
  if (!authed()) return;
  String what = server.arg("what"), file = server.arg("file");
  if (file.length()) {
    if (!LittleFS.exists(file)) return sendError(404, "nėra failo");
    playFile(file, "per API");
  } else if (what == "bendras" || what == "pries") {
    ring(what == "pries", "bandomasis per API");
  } else {
    int i = signalIndex(what);
    if (i < 0) return sendError(400, "what: bendras | pries | BALTAS | GELTONAS | RAUDONAS");
    if (signalFile[i].isEmpty()) return sendError(404, what + " garsas nepriskirtas");
    playFile(signalFile[i], SIGNALS[i], true);
  }
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void handleStop() {
  if (!authed()) return;
  audio.stopSong();
  logf("sustabdyta per API");
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void handleLog() {  // log.old + log.txt, seniausi viršuje
  if (!authed()) return;
  File a = LittleFS.open(LOG_OLD), b = LittleFS.open(LOG_FILE);
  server.setContentLength((a ? a.size() : 0) + (b ? b.size() : 0));
  server.send(200, "text/plain; charset=utf-8", "");
  static uint8_t buf[1024];
  for (File* f : {&a, &b})
    while (*f && f->available()) {
      size_t n = f->read(buf, sizeof buf);
      server.sendContent((const char*)buf, n);
    }
}

void handlePin() {
  if (!authed()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) return sendError(400, "neteisingas JSON");
  String p = in["pin"] | "";
  if (!validPin(p)) return sendError(400, "PIN – 4–12 skaitmenų");
  savePin(p);
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void handleUpdate() {
  if (!authed()) return;
  updateRequested = true;  // tikrina loop(); rezultatas – /status "update"
  updateInfo = "tikrinama...";
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void startWeb() {
  const char* headers[] = {"X-Pin", "Content-Length"};
  server.collectHeaders(headers, 2);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on("/config", HTTP_POST, handlePostConfig);
  server.on("/files", HTTP_GET, handleFiles);
  server.on("/file", HTTP_GET, handleGetFile);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/play", HTTP_POST, handlePlay);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/pin", HTTP_POST, handlePin);
  server.on("/update", HTTP_POST, handleUpdate);
  server.onNotFound([] { sendError(404, "nėra tokio adreso"); });
  server.begin();
  // Arduino core 3.3: pirmas hostByName po IP gavimo kviečia dns_clear_cache() be TCPIP užrakto. Jei tuo metu
  // SNTP laukia DNS atsakymo – lwIP assert ir perkrovimas (atnaujinimų tikrinimas). Todėl talpyklą išvalom
  // dabar ("localhost" – be tinklo), o SNTP paleidžiam tik po to.
  IPAddress lo;
  WiFi.hostByName("localhost", lo);
  configTzTime(TZ_VILNIUS, "pool.ntp.org", "time.google.com");
  if (MDNS.begin(HOSTNAME)) MDNS.addService("skambutis", "tcp", 80);
  WiFi.setSleep(false);  // be energijos taupymo API atsako per ms, ne šimtus ms
  webStarted = true;
  logf("API: http://%s/ (%s.local)", WiFi.localIP().toString().c_str(), HOSTNAME);
}

// --- serial -----------------------------------------------------------------

void heartbeat();

// Nauji oro pavojaus pranešimai (id > alertId) mokyklos vietovei: "play" – groja tą patį signalą, "log" – tik įrašo.
// Per vieną patikrinimą keli (GELTONA -> RAUDONA) – groja naujausią. Senesnis nei 15 min – negroja (po elektros dingimo).
void checkAlerts() {
  char hm[8] = "--:--";
  struct tm tm;
  if (nowLocal(tm)) strftime(hm, sizeof hm, "%H:%M", &tm);
  NetworkClientSecure client;
  HTTPClient http;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(5);  // s; numatyta 120 – silpnas WiFi užlaikytų loop() (ir watchdog)
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, String(ALERTS_URL) + "&t=" + millis())) { alertInfo = "klaida: adresas"; return; }
  int code = http.GET();
  if (code != 200) { alertInfo = String("klaida: HTTP ") + code + ", " + hm; return; }
  JsonDocument filter, d;
  filter[0]["id"] = true;
  filter[0]["date_gmt"] = true;
  filter[0]["content"]["rendered"] = true;
  if (deserializeJson(d, http.getString(), DeserializationOption::Filter(filter)) || !d.is<JsonArray>()) {
    alertInfo = String("klaida: netinkamas atsakymas, ") + hm;
    return;
  }
  alertInfo = String("gerai, ") + hm;
  JsonArray a = d.as<JsonArray>();
  uint32_t newest = alertId;
  for (JsonObject x : a) newest = std::max(newest, (uint32_t)(x["id"] | 0));
  if (alertId == 0) {  // pirmas kartas: esami pranešimai jau praėję
    alertId = newest;
    prefs.putUInt("alert_id", alertId);
    logf("perspėjimai: stebima lt72.lt nuo #%u", alertId);
    return;
  }
  int play = -1;
  for (int i = a.size() - 1; i >= 0; i--) {  // REST – naujausi pirmi; apdorojam nuo seniausio
    JsonObject x = a[i];
    uint32_t id = x["id"] | 0;
    if (id <= alertId) continue;
    std::string html = x["content"]["rendered"] | "";
    int lvl = alertLevel(html);
    if (lvl < 0) continue;  // ne oro pavojus: vėjas, policija, sirenų patikrinimas
    std::string area = alertArea(html);
    int y, mo, dd, hh, mi, ss;
    const char* g = x["date_gmt"] | "";
    time_t sent = sscanf(g, "%d-%d-%dT%d:%d:%d", &y, &mo, &dd, &hh, &mi, &ss) == 6 ? utcToEpoch(y, mo, dd, hh, mi, ss) : 0;
    bool ours = areaMatches(area, alertPlace.c_str()), fresh = time(nullptr) - sent < 900;
    const char* act = !ours ? "ne mūsų vietovė" : !fresh ? "senas – negrojama" : alertsMode == "play" ? "groja signalas" : "tik žurnalas";
    logf("perspėjimas #%u: %s, %s – %s", id, LEVELS[lvl], area.c_str(), act);
    char when[24] = "";
    struct tm st;
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime_r(&sent, &st));
    alertLast = String(when) + " " + LEVELS[lvl] + " – " + area.c_str() + (ours ? "" : " (ne mūsų)");
    if (ours && fresh) play = lvl;
  }
  if (newest != alertId) prefs.putUInt("alert_id", alertId = newest);
  if (play < 0 || alertsMode != "play") return;
  if (signalFile[play].isEmpty()) logf("perspėjimas: %s garsas nepriskirtas – negroja", SIGNALS[play]);
  else playFile(signalFile[play], SIGNALS[play], true);
}

void rtcStatus() {
  time_t t;
  bool lost;
  if (!rtcOk || !rtcRead(t, lost)) { logf("RTC: nerastas (laidai SDA=%u, SCL=%u, VCC=3V3?)", RTC_SDA, RTC_SCL); return; }
  struct tm tm;
  char buf[24];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", localtime_r(&t, &tm));
  logf("RTC: %s, skirtumas nuo sistemos %+lld s, %s, %.2f °C", buf, (long long)(t - time(nullptr)),
       lost ? "LAIKAS BUVO SUSTOJĘS (baterija?)" : "laikas nenutrūkęs", rtcTemp());
}

void status() {
  struct tm tm;
  bool synced = nowLocal(tm);
  logf("versija %s", FW_VERSION);
  Serial.printf("PIN %s\n", pin.c_str());  // ne į žurnalą: jis siunčiamas laiškais
  logf("WiFi: %s %s, RSSI %d dBm, portalas %s", WiFi.isConnected() ? "prisijungta" : "NEPRISIJUNGTA",
       WiFi.localIP().toString().c_str(), WiFi.RSSI(), wm.getConfigPortalActive() ? "ĮJUNGTAS" : "išjungtas");
  if (!synced) logf("laikas nesinchronizuotas – skambučiai neveikia");
  else {
    int minute = tm.tm_hour * 60 + tm.tm_min, nb = nextBell(sched, (tm.tm_wday + 6) % 7, minute);
    if (nb < 0) logf("šiandien daugiau skambučių nėra");
    else logf("sekantis skambutis %02d:%02d", nb / 60, nb % 60);
  }
  logf("heap %u, psram %u, fs %u/%u", ESP.getFreeHeap(), ESP.getFreePsram(), LittleFS.usedBytes(), LittleFS.totalBytes());
  rtcStatus();
}

void handleSerial() {
  switch (Serial.read()) {
    case 'p': ring(false, "bandomasis"); break;
    case 'r': ring(true, "bandomasis (prieš)"); break;
    case 's': audio.stopSong(); break;
    case 'i': status(); break;
    case 'c': rtcStatus(); break;
    case 'b': heartbeat(); break;
    case 'H':  // watchdog testas: užstrigti tyčia
      logf("watchdog testas: užstringu tyčia...");
      while (true) delay(1);
    case 'u': updateRequested = true; logf("tikrinamas atnaujinimas..."); break;
    case 'A':
      prefs.putUInt("alert_id", alertId = strtoul(Serial.readStringUntil('\n').c_str(), nullptr, 10));
      logf("perspėjimai: matyti iki #%u", alertId);
      break;
    case 'T': {  // laikas iš kompiuterio, kai nėra interneto: "T1792411200\n"
      time_t t = strtoll(Serial.readStringUntil('\n').c_str(), nullptr, 10);
      if (t < 1700000000) { logf("blogas laikas"); break; }
      struct timeval tv = {t, 0};
      settimeofday(&tv, nullptr);
      logf(rtcOk && rtcWrite(t) ? "laikas nustatytas (sistema ir RTC)" : "laikas nustatytas tik sistemai – RTC nerastas");
      break;
    }
    case 'x': {  // testinė pamoka: "prieš" po 1 min, pradžia po 3, pabaiga po 4 (jei prieš = 2 min)
      struct tm tm;
      if (!nowLocal(tm)) { logf("laikas nesinchronizuotas"); break; }
      int m = tm.tm_hour * 60 + tm.tm_min + 3;
      sched.lessons.push_back({m % 1440, (m + 1) % 1440});
      sched.days = 0x7F;
      logf("testinė pamoka %02d:%02d-%02d:%02d", m / 60 % 24, m % 60, (m + 1) / 60 % 24, (m + 1) % 60);
      break;
    }
    case 'w': wm.resetSettings(); ESP.restart(); break;
    case 'n': {  // WiFi tinklų paieška
      int n = WiFi.scanNetworks();
      logf("rasta %d tinklų", n);
      for (int i = 0; i < n && i < 10; i++) {
        wifi_auth_mode_t a = WiFi.encryptionType(i);
        const char* auth = a == WIFI_AUTH_OPEN ? "atviras" : a == WIFI_AUTH_WPA2_ENTERPRISE || a == WIFI_AUTH_WPA3_ENT_192 ? "ENTERPRISE"
                         : a == WIFI_AUTH_WPA3_PSK || a == WIFI_AUTH_WPA2_WPA3_PSK ? "WPA3" : "WPA2 slaptažodis";
        logf("  %-32s %4d dBm  kanalas %2d  %s", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i), auth);
      }
      WiFi.scanDelete();
      break;
    }
    case 't':  // ar I2S laidai nesujungti su GND/3V3: pullup=0 -> GND, pulldown=1 -> 3V3
      audio.stopSong();
      for (uint8_t p : {I2S_BCK, I2S_LCK, I2S_DIN}) {
        pinMode(p, INPUT_PULLUP); delay(5); int up = digitalRead(p);
        pinMode(p, INPUT_PULLDOWN); delay(5); int down = digitalRead(p);
        logf("GPIO %u: %s", p, !up ? "SUJUNGTAS SU GND" : down ? "SUJUNGTAS SU 3V3" : "ok");
      }
      logf("testas baigtas – perkraunu (I2S kanalo iš naujo neatkursi)");
      delay(100);
      ESP.restart();
  }
}

// --- setup / loop -----------------------------------------------------------

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "įjungtas maitinimas";
    case ESP_RST_SW: return "programinis perkrovimas (atnaujinimas / komanda)";
    case ESP_RST_PANIC: return "PROGRAMOS KLAIDA (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG – programa buvo užstrigusi";
    case ESP_RST_BROWNOUT: return "ĮTAMPOS KRITIMAS (brownout)";
    case ESP_RST_EXT: return "RST mygtukas / USB";
    case ESP_RST_USB: return "USB";
    default: return "kita";
  }
}

void heartbeat() {  // kas valandą – 72 h testui ir gedimų paieškai
  time_t t;
  bool lost;
  float temp = rtcOk && rtcRead(t, lost) ? rtcTemp() : NAN;
  logf("būsena: veikia %lu h, heap %u (min %u), WiFi %d dBm, RTC %.1f °C, %s", millis() / 3600000UL, ESP.getFreeHeap(),
       ESP.getMinFreeHeap(), WiFi.isConnected() ? WiFi.RSSI() : 0, temp, FW_VERSION);
}

void setup() {
  Serial.begin(115200);
  setenv("TZ", TZ_VILNIUS, 1);  // po programinio perkrovimo laikas jau yra – žurnale iš karto vietinis, ne UTC
  tzset();
  Audio::audio_info_callback = [](Audio::msg_t m) {
    if (m.e == Audio::evt_eof || strstr(m.msg, "rror")) logf("audio %s: %s", m.s, m.msg);
  };
  fsOk = LittleFS.begin(true);
  logf("skambutis %s – įsijungė: %s", FW_VERSION, resetReason());
  if (!fsOk) logf("KLAIDA: LittleFS");
  audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  // dešinys -16 dB: garsas girdisi ir per simetrinį PA įėjimą / TRRS ausines (jos praleidžia tik L-R)
  audio.setBalance(16.0f);
  loadConfig();

  prefs.begin("skambutis");
  pin = prefs.getString("pin", "");
  alertId = prefs.getUInt("alert_id", 0);
  if (!validPin(pin)) {
    char p[7];
    snprintf(p, sizeof p, "%06lu", (unsigned long)(esp_random() % 1000000));
    savePin(p);
    logf("sugeneruotas naujas PIN (rodomas tik USB prievade) – pakeisti galima WiFi nustatymų puslapyje arba programoje");
    Serial.printf("PIN %s\n", p);
  }

  sntp_set_time_sync_notification_cb([](struct timeval*) { ntpSynced = true; });  // SNTP paleidžia startWeb()
  // laikas iš DS3231, kol nėra interneto; "sustojęs" laikrodžiu nepasitikim
  rtcOk = rtcBegin(RTC_SDA, RTC_SCL);
  time_t t;
  bool lost;
  if (!rtcOk) logf("RTC: nerastas – laikas tik iš interneto");
  else if (rtcRead(t, lost) && !lost && t > 1700000000) {
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    logf("RTC: laikas nustatytas iš laikrodžio");
  } else logf("RTC: laikas buvo sustojęs (išsekusi baterija arba dar nenustatytas) – laukiam interneto");

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  pinParam = new WiFiManagerParameter("pin", "Programos PIN (4–12 skaitmenų)", pin.c_str(), 12);
  wm.addParameter(pinParam);
  auto savePinParam = [] {
    String p = pinParam->getValue();
    if (p != pin && validPin(p)) savePin(p);
  };
  wm.setSaveParamsCallback(savePinParam);
  wm.setSaveConfigCallback(savePinParam);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  // ponytail: portalas 5 min po starto be WiFi, paskui tik bandom jungtis; iš naujo – perkrovus
  wm.setConfigPortalTimeout(300);
  if (wm.autoConnect(AP_NAME, AP_PASS)) logf("WiFi: %s", WiFi.localIP().toString().c_str());
  else logf("WiFi neprisijungta – nustatymai per tinklą %s, http://192.168.4.1", AP_NAME);

  // watchdog: jei loop() užstringa ilgiau nei 60 s – perkrauna (ilgiausi darbai: OTA ~40 s, maitinamas viduje)
  esp_task_wdt_config_t wdt = {.timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(nullptr);
}

void loop() {
  esp_task_wdt_reset();
  audio.loop();
  wm.process();
  checkBell();
  if (webStarted) server.handleClient();
  else if (WiFi.isConnected() && !wm.getConfigPortalActive()) startWeb();  // portalas ir API – abu ant :80

  if (ntpSynced) {  // kiekvienas NTP sinchronizavimas (~kas valandą) atnaujina DS3231
    ntpSynced = false;
    if (rtcOk) logf(rtcWrite(time(nullptr)) ? "RTC: atnaujintas iš interneto" : "KLAIDA: RTC įrašymas");
  }
  static uint32_t lastRtcCheck = 0;  // laikrodis prijungtas vėliau arba jo laikas buvo sustojęs
  if (millis() - lastRtcCheck > 60000) {
    lastRtcCheck = millis();
    if (!rtcOk && (rtcOk = rtcProbe())) logf("RTC: rastas");
    time_t t;
    bool lost;
    if (rtcOk && time(nullptr) > 1700000000 && rtcRead(t, lost) && lost)
      logf(rtcWrite(time(nullptr)) ? "RTC: laikas buvo sustojęs – įrašytas iš naujo" : "KLAIDA: RTC įrašymas");
  }

  static uint32_t lastTry = 0;
  if (!WiFi.isConnected() && !wm.getConfigPortalActive() && millis() - lastTry > 60000) {
    lastTry = millis();
    WiFi.reconnect();
  }

  // nauja versija: patvirtinam, kai 2 min veikia ir prisijungė prie WiFi; kitaip po 10 min – atgal į seną
  static bool pending = otaPendingVerify();
  if (pending && webStarted && millis() > 120000) {
    esp_ota_mark_app_valid_cancel_rollback();
    pending = false;
    logf("versija %s patvirtinta", FW_VERSION);
  } else if (pending && millis() > 600000) {
    logf("versija %s neprisijungė prie WiFi per 10 min – grįžtam prie ankstesnės", FW_VERSION);
    delay(200);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }

  static uint32_t nextHeartbeat = 3600000;
  if (millis() > nextHeartbeat) {
    nextHeartbeat += 3600000;
    heartbeat();
  }

  // perspėjimai kas 30 s; ne grojant (užklausa stabdo loop – trūkinėtų garsas) ir ne paskutinę minutę prieš skambutį:
  // su silpnu WiFi (DNS + TLS + skaitymas) užklausa gali trukti ~30 s, o skambutis turi pasigirsti tiksliai
  static uint32_t nextAlertCheck = 60000;
  if (alertsMode != "off" && webStarted && WiFi.status() == WL_CONNECTED && millis() > nextAlertCheck && !audio.isRunning()) {
    struct tm tm;
    bool soon = false;
    if (nowLocal(tm)) {
      int m = tm.tm_hour * 60 + tm.tm_min, nb = nextBell(sched, (tm.tm_wday + 6) % 7, m);
      soon = nb == m + 1;
    }
    if (!soon) {
      nextAlertCheck = millis() + 30000;
      checkAlerts();
    }
  }

  static uint32_t nextUpdateCheck = 180000;  // pirmas – 3 min po įsijungimo, paskui kas valandą
  if (!pending && webStarted && WiFi.status() == WL_CONNECTED && (updateRequested || millis() > nextUpdateCheck)) {
    updateRequested = false;
    nextUpdateCheck = millis() + (checkUpdate() ? 3600000 : 900000);
  }

  if (Serial.available()) handleSerial();
}
