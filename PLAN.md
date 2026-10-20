# Skambutis v2 – ESP32-S3 + PCM5102A

Laptopas su `skambutis.py` keičiamas atskiru įrenginiu, prijungtu prie mokyklos
PA per 3.5 mm. Viskas lokaliai: įrenginys pats yra ir skambutis, ir
konfigūravimo puslapis. Iš interneto – tik laikas (NTP) ir firmware
atnaujinimai (GitHub Releases). Nuotoliniam valdymui – RDP į mokyklos PC.

```
[PC naršyklė] ──LAN──▶ http://skambutis.local  (ESP32: UI + config + mp3 LittleFS)
                              │
                              └─ išeinantis internetas ─▶ GitHub Releases (OTA), NTP
```

## Aparatūra

- ESP32-S3-N16R8 (16 MB flash, 8 MB PSRAM)
- GY-PCM5102 (PCM5102A I2S DAC, line out ~2.1 Vrms)
- Rekomenduojama: DS3231 RTC (laikas be interneto), ground-loop isolator (jei ūžia)

### PCM5102 jumperiai (galinė pusė)

| Jumper | Į | Kam |
|---|---|---|
| H1L FLT | L | normalus filtras |
| H2L DEMP | L | be de-emphasis |
| **H3L XSMT** | **H** | **unmute – be šito tyla** |
| H4L FMT | L | I2S |
| SCK (priekyje) | GND | vidinis PLL |

### Laidai

| PCM5102 | ESP32-S3 |
|---|---|
| VIN | 5Vin (būtina užlituoti IN-OUT pad'us; iš 3V3 DAC traška ir numuša ESP32 maitinimą) |
| GND | GND (bet kuris, pvz. dešinėje viršuje) |
| BCK | GPIO 4 |
| LCK | GPIO 5 |
| DIN | GPIO 6 |
| SCK | GND (trumpas laidukas ant DAC) |

Neliesti: GPIO 26–37 (flash/octal PSRAM), 0/3/45/46 (strapping), 19/20 (USB), 43/44 (UART0).
I2C RTC (DS3231): SDA = GPIO 8, SCL = GPIO 18 (ne 9 – naujoje plokštėje 9 kontaktas neveikė), VCC = 3V3.

Į PA – **LINE/AUX** įėjimas, ne MIC. Maitinimas – atskiras 5 V/1 A USB adapteris.

## Programinė įranga

C++ / Arduino core 3.x (pioarduino) / PlatformIO, biblioteka `ESP32-audioI2S` 3.4.7.

Repo `skambutis-esp32` (senasis `skambuteliukas` – laptopo versija, lieka atskirai):

```
platformio.ini, partitions.csv   # app0/app1 po 3 MB (OTA), LittleFS ~9.9 MB
src/main.cpp      # wifi (STA + AP "Skambutis-admin"), mDNS, NTP, ciklas
src/schedule.*    # bell_times() portas iš skambutis.py + native testai
src/audio.*       # grojimas, stop, signalų prioritetas
src/web.*         # API + gzip'intas index.html
src/ota.*         # GitHub Releases + rollback
web/index.html    # skambutis.py PAGE pagrindu
data/             # 0 fazės testiniai garsai (ne git)
```

API (POST/DELETE – su PIN antrašte `X-Pin`):

```
GET  /            UI                      GET /status   laikas, sekantis, fw, vieta, log
GET  /config      POST /config            GET /files    POST /upload   DELETE /file?n=
POST /play?s=test|BALTAS|GELTONAS|RAUDONAS                POST /stop
GET  /export      POST /import            (zip: config + garsai – atsarginė kopija)
```

Laikas: NTP, TZ `EET-2EEST,M3.5.0/3,M10.5.0/4`; jei NTP užblokuotas – `Date`
antraštė iš HTTPS atsakymo; be interneto – RTC.

OTA: kas valandą ir įkrovos metu `releases/latest/download/firmware.json`;
neatnaujinama, jei iki skambučio < 10 min; po atnaujinimo, jei per 3 min nėra
WiFi + garso – `Update.rollBack()`. CI (GitHub Actions): `pio test -e native`
→ `pio run` → release su `firmware.bin` + `firmware.json`.

## Fazės

| # | Fazė | Priėmimas | ~d |
|---|---|---|---|
| 0 | Litavimas, testinis grojimas iš LittleFS per PA | girdėti, neūžia, WAV ir MP3 | 0.5 |
| 1 | WiFi (STA+AP portalas), NTP, schedule + testai, skambina pagal config | visą dieną laiku | 1–2 |
| 2 | Web UI + API + upload (garsumo normalizavimas, MP3 48 kHz) + PIN + mDNS + eksportas/importas | viskas keičiama iš naršyklės | 1.5–2 |
| 3 | OTA + CI + rollback | push → įrenginys atsinaujina; blogas build grįžta atgal | 1 |
| 4 | Watchdog, RTC, log, 72 h testas, montavimas | 72 h be priežiūros | 1–1.5 |

## Rizikos

| Rizika | Ką daryti |
|---|---|
| WiFi client isolation – PC nemato įrenginio | AP `Skambutis-admin` (192.168.4.1) arba IT išimtis |
| WPA2-Enterprise / captive portal | Enterprise palaikomas; captive – reikia kito SSID iš IT |
| NTP blokuojamas | `Date` antraštė, RTC |
| PCM5102 tyli | XSMT=H, SCK→GND |
| Klonų plokštės skiriasi (RGB LED 48 ar 38) | patikrinti 0 fazėje |
| LittleFS failo vardas ≤ 63 simb. | upload'e saugoti trumpu ID, rodomą vardą laikyti config'e |
