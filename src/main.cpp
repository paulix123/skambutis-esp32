// 0 fazė: ar girdisi per PA. Groja failus iš LittleFS, valdoma per Serial.
//   p - groti  n - kitas  s - stop  + / - garsumas  l - sąrašas  i - info
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "Audio.h"

// PCM5102A: BCK, LCK, DIN. SCK -> GND
constexpr uint8_t I2S_BCK = 4, I2S_LCK = 5, I2S_DIN = 6;

Audio audio;
std::vector<String> files;
size_t cur = 0;
uint8_t vol = 15;  // 0..21

void scan(File dir) {
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) { scan(f); continue; }
    String n = f.path();
    n.toLowerCase();
    if (n.endsWith(".mp3") || n.endsWith(".wav")) files.push_back(f.path());
  }
}

void play() {
  if (files.empty()) { Serial.println("nera failu - pio run -t uploadfs"); return; }
  Serial.printf("groja [%u] %s\n", cur, files[cur].c_str());
  if (!audio.connecttoFS(LittleFS, files[cur].c_str())) Serial.println("KLAIDA: nepavyko atidaryti");
}

void info() {
  Serial.printf("heap %u, psram %u/%u, fs %u/%u, vol %u\n", ESP.getFreeHeap(), ESP.getFreePsram(),
                ESP.getPsramSize(), LittleFS.usedBytes(), LittleFS.totalBytes(), vol);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Audio::audio_info_callback = [](Audio::msg_t m) { Serial.printf("audio %s: %s\n", m.s, m.msg); };
  if (!LittleFS.begin(true)) Serial.println("KLAIDA: LittleFS");
  scan(LittleFS.open("/"));
  for (size_t i = 0; i < files.size(); i++) Serial.printf("[%u] %s\n", i, files[i].c_str());
  audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
  audio.setVolume(vol);
  info();
  play();  // groja iš karto - galima tikrinti ir be Serial
}

void loop() {
  audio.loop();
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case 'p': play(); break;
    case 'n': if (!files.empty()) cur = (cur + 1) % files.size(); play(); break;
    case 's': audio.stopSong(); break;
    case '+': audio.setVolume(vol = min(vol + 1, 21)); info(); break;
    case '-': audio.setVolume(vol = vol ? vol - 1 : 0); info(); break;
    case 'l': for (size_t i = 0; i < files.size(); i++) Serial.printf("[%u] %s\n", i, files[i].c_str()); break;
    case 'i': info(); break;
  }
}
