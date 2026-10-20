// Skambučio dėžutė: ESP32-S3-N16R8 DevKitC + GY-PCM5102 + DS3231 (ZS-042) + 1000 µF kondensatorius.
// Visi matmenys mm. Prieš spausdinant pamatuok savo plokštes ir pataisyk "Plokštės" bloką.
// STL:  openscad -D 'part="box"' -o box.stl dezute.scad   (taip pat "lid", "test" – sienelė su skylėmis bandymui)
//
// Moduliai su kontaktais aukštyn, jungiami Dupont laidais; RTC – su gamykliniais kampiniais kontaktais.
// Visos plokštės guli komponentais aukštyn. ESP32: USB į sienelę x=0, kontaktų eilė su 3V3…5Vin – į DAC pusę.
// DAC: lizdas išeina per ilgąją kraštinę, tad DAC prie sienelės x=0 guli ilgąja (32 mm) puse,
//      lizdas – tos kraštinės gale, arčiau ESP32 (USB-C).
// Išdėstymas (vaizdas iš viršaus), USB ir 3.5 mm lizdas – toje pačioje sienelėje x=0:
//   y ^  [ RTC ]>>Dupont>>
//     |  [ DAC ]
//     |  [ ESP32 ............ antena ][laisva]
//     +--------------------------------------> x
part = "all";  // "box" | "lid" | "test" | "preview" | "all"

/* [Plokštės] */
esp = [57, 28];   // ESP32 PCB: ilgis be antenos, plotis (pamatuota)
esp_ant = 6.5;    // modulio antena išsikiša už PCB galo (pamatuota)
esp_ant_w = 18;   // tarpas ribotuvuose antenai (pamatuota)
dac = [17.5, 32]; // PCM5102: gylis nuo sienelės (trumpoji kraštinė), ilgis palei sienelę – pamatuota, +0.5 po bandymo
rtc = [38, 22];   // DS3231 ZS-042 (be kontaktų), pamatuota
rtc_h = 7;        // aukštis su baterija; kampiniai kontaktai dar +5 mm (pamatuota)
rtc_pins = 22;    // 6 kampiniai kontaktai + Dupont kištukai, išsikiša į +x
rtc_pin_gap = 15; // tarpas ribotuvuose RTC galuose kontaktams (po bandymo)
rtc_back = 1;     // kitame gale tik 4 tušti pad'ai (HW-084), tad beveik prie sienelės
tape = 3.5;       // putplasčio juosta po plokštėmis: kontaktų galai apačioje kyšo ~3 mm
pcb = 1.6;
usb_h = 3.3;      // USB-C lizdo aukštis virš ESP32 PCB
jack_y = 4;       // 3.5 mm lizdo centras nuo DAC galo, artimesnio ESP32 (pamatuota 3, po bandymo +1)
jack_z = 2;       // lizdo centras virš DAC PCB (po bandymo nuleista 1 mm)
jack_d = 8;       // skylė kištukui (storesniems kištukams – 9)

/* [Dėžutė] */
wall = 2;
floor_t = 2;
lid_t = 2;
gap = 4;            // tarpas tarp plokščių
antenna_free = 8;   // laisva vieta už antenos galo
inner_h = 30;       // Dupont kištukai ant kontaktų aukštyn baigiasi ~24 mm + laido lenkimas iki dangtelio lūpos
fit = 0.15;         // dangtelio lūpos tarpas (0.3 buvo per laisvas)
crush = 0.35;       // trinties briaunelės už lūpos – susispaudžia ir laiko dangtelį; laisvas – didink, ankštas – mažink
r = 3;              // kampų apvalinimas

inner = [max(esp[0] + esp_ant + antenna_free, rtc_back + rtc[0] + rtc_pins + 2), esp[1] + gap + dac[1] + gap + rtc[1]];
outer = [inner[0] + 2 * wall, inner[1] + 2 * wall];
z0 = floor_t + tape;  // plokščių apačia

esp_pos = [0, 0];
dac_pos = [0, esp[1] + gap];
rtc_pos = [rtc_back, esp[1] + gap + dac[1] + gap];
assert(tape + rtc_h + 5 + 3 <= inner_h - 3, "RTC su kontaktais neleidžia uždėti dangtelio");
rtc_size = rtc;
// RTC eilė per dvi eiles nuo ESP32 (~29 mm), tad jos kontaktai antenos netrukdo

$fn = 48;

module rounded(size, h, rr = r) {
  hull() for (x = [rr, size[0] - rr], y = [rr, size[1] - rr]) translate([x, y, 0]) cylinder(r = rr, h = h);
}

// kampiniai L ribotuvai aplink plokštę, kad ji nesislankiotų ant juostos;
// abi L dalys ilgesnės per t ir persidengia kampe tūriu – kitaip lieka ne manifold briaunos
// gx0 / gx1: laisvas tarpas viduryje x=0 / x=galo ribotuvų (kontaktams, antenai); 0 – įprasti kampai
module stops(pos, size, gx0 = 0, gx1 = 0, h = 5, t = 1.2, l = 6, c = 0.4) {
  for (sx = [0, 1], sy = [0, 1]) {
    x = pos[0] + sx * size[0] + (sx ? c : -c - t);
    y = pos[1] + sy * size[1] + (sy ? c : -c - t);
    g = sx ? gx1 : gx0;
    ly = g ? max(t, (size[1] + 2 * c + 2 * t - g) / 2) : l + t;  // y ribotuvo ilgis nuo kampo
    translate([wall + x, wall + (sy ? pos[1] + size[1] + c + t - ly : pos[1] - c - t), floor_t - 0.5]) cube([t, ly, h + 0.5]);
    translate([wall + pos[0] + sx * (size[0] - l) + (sx ? c : -c - t), wall + y, floor_t - 0.5]) cube([l + t, t, h + 0.5]);
  }
}

module box() {
  difference() {
    union() {
      difference() {
        rounded(outer, floor_t + inner_h);
        translate([wall, wall, floor_t]) rounded(inner, inner_h + 1, r - wall / 2);
      }
      intersection() {  // ribotuvai neišlenda pro sieneles (0.5 mm į jas įeina – kad susilietų)
        translate([wall - 0.5, wall - 0.5, 0]) cube([inner[0] + 1, inner[1] + 1, floor_t + inner_h]);
        union() { stops(esp_pos, esp, 0, esp_ant_w); stops(dac_pos, dac); stops(rtc_pos, rtc_size, rtc_pin_gap, rtc_pin_gap); }
      }
    }
    // abi USB-C jungtys (kištuko korpusas ~12.5 x 7.5)
    translate([-1, wall + esp_pos[1], z0 + pcb + usb_h / 2 - 4.5]) cube([wall + 2, esp[1], 9]);
    // 3.5 mm lizdas
    translate([-1, wall + dac_pos[1] + jack_y, z0 + pcb + jack_z]) rotate([0, 90, 0]) cylinder(d = jack_d, h = wall + 2);
  }
}

module lid() {
  lip = 4;
  lw = [inner[0] - 2 * fit, inner[1] - 2 * fit];  // lūpos išorė
  difference() {
    union() {
      rounded(outer, lid_t);
      // lūpa įeina į dėžutę ir laikosi trintimi
      translate([wall + fit, wall + fit, lid_t]) difference() {
        rounded([inner[0] - 2 * fit, inner[1] - 2 * fit], lip, r - wall / 2);
        translate([1.6, 1.6, -1]) rounded([inner[0] - 2 * fit - 3.2, inner[1] - 2 * fit - 3.2], lip + 2, 1);
      }
      // trinties briaunelės: po 3 kiekvienoje pusėje, 0.3 mm įeina į lūpą (vientisas kūnas)
      translate([wall + fit, wall + fit, lid_t]) for (f = [0.25, 0.5, 0.75], side = [0, 1]) {
        translate([side ? lw[0] - 0.3 : -crush, f * lw[1] - 0.4, 0]) cube([crush + 0.3, 0.8, lip]);
        translate([f * lw[0] - 0.4, side ? lw[1] - 0.3 : -crush, 0]) cube([0.8, crush + 0.3, lip]);
      }
    }
    // ventiliacija virš ESP32 (stabilizatorius) ir DAC; antenos galo nelieskit
    for (i = [0:5]) translate([wall + 12 + i * 7, wall + 5, -1]) cube([2.5, inner[1] - 10, lid_t + 2]);
  }
}

// peržiūra: plokštės spalvotais blokais (ESP32 juoda, antena pilka, DAC violetinė, RTC mėlyna, Dupont oranžinė)
module boards() {
  translate([wall, wall, z0]) {
    color("black") cube([esp[0], esp[1], pcb]);
    color("gray") translate([esp[0] - 12, (esp[1] - esp_ant_w) / 2, pcb]) cube([12 + esp_ant, esp_ant_w, 3]);
    color("purple") translate(dac_pos) cube([dac[0], dac[1], pcb]);
    color("royalblue") translate(rtc_pos) cube([rtc[0], rtc[1], rtc_h]);
    color("orange") translate([rtc_pos[0] + rtc[0], rtc_pos[1] + 2, rtc_h]) cube([rtc_pins, rtc[1] - 4, 3]);
    color("orange") for (y = [1, esp[1] - 3.5]) translate([3, y, pcb]) cube([esp[0] - 14, 2.5, 16]);
    color("orange") translate([rtc_pos[0] - rtc_back, rtc_pos[1] + 5, rtc_h]) cube([rtc_back, rtc[1] - 10, 3]);
    color("silver") for (y = [4, esp[1] - 13]) translate([-1, y, pcb]) cube([7.5, 9, usb_h]);   // USB-C
    color("dimgray") translate([-1, dac_pos[1] + jack_y - 3, pcb]) cube([12, 6, 2 * jack_z]);    // 3.5 mm lizdas
  }
}

// bandymui: tik sienelė su USB ir 3.5 mm skylėmis + 12 mm dugno – patikrinti su tikromis plokštėmis
module test() {
  intersection() { box(); translate([-1, -1, -1]) cube([wall + 13, outer[1] + 2, floor_t + inner_h + 2]); }
}

if (part == "box") box();
else if (part == "test") test();
else if (part == "preview") { box(); boards(); }
else if (part == "lid") lid();
else { box(); translate([0, outer[1] + 10, 0]) lid(); }
