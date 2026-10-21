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
esp = [57, 28.5]; // ESP32 PCB: ilgis be antenos, plotis (pamatuota 28, +0.5 po bandymo)
esp_ant = 6.5;    // modulio antena išsikiša už PCB galo (pamatuota)
esp_ant_w = 18;   // tarpas ribotuvuose antenai (pamatuota)
dac = [17.5, 32]; // PCM5102: gylis nuo sienelės (trumpoji kraštinė), ilgis palei sienelę – pamatuota, +0.5 po bandymo
rtc = [38, 22];   // DS3231 ZS-042 (be kontaktų), pamatuota
rtc_h = 7;        // aukštis su baterija; kampiniai kontaktai dar +5 mm (pamatuota)
rtc_pins = 22;    // 6 kampiniai kontaktai + Dupont kištukai, išsikiša į +x
rtc_pin_gap = 15.5; // tarpas ribotuvuose RTC galuose kontaktams (po bandymų)
rtc_back = 1;     // kitame gale tik 4 tušti pad'ai (HW-084), tad beveik prie sienelės
tape = 3.5;       // putplasčio juosta po plokštėmis: kontaktų galai apačioje kyšo ~3 mm
pcb = 1.6;
usb_h = 3.3;      // USB-C lizdo aukštis virš ESP32 PCB
usb_side = 2;     // USB anga siauresnė iš abiejų šonų (po bandymo)
usb_top = 2;      // USB anga žemesnė iš viršaus (po bandymo)
jack_y = 6;       // 3.5 mm lizdo centras nuo DAC galo, artimesnio ESP32 (pamatuota 3, po bandymų +3)
jack_z = 2;       // lizdo centras virš DAC PCB (po bandymo nuleista 1 mm)
jack_d = 8;       // skylė kištukui (storesniems kištukams – 9)

/* [Dėžutė] */
wall = 2;
floor_t = 2;
lid_t = 2;
gap = 4;            // tarpas tarp plokščių
antenna_free = 8;   // laisva vieta už antenos galo
inner_h = 30;       // Dupont kištukai ant kontaktų aukštyn baigiasi ~24 mm + laido lenkimas iki dangtelio lūpos
fit = 0.2;          // dangtelio lūpos tarpas
// dangtis užsispaudžia dviem lanksčiais dantukais (ilgosiose sienelėse, per vidurį – tinka abiem kryptimis)
snap_w = 10;        // dantuko plotis
snap_l = 10;        // ilgis nuo dangčio – kuo ilgesnis, tuo lankstesnis
snap_t = 1.2;       // storis
snap_hook = 0.6;    // kiek kabliukas išsikiša už lūpos; per silpnai laiko – 0.8, per sunku nuimti – 0.4
groove_d = 0.9;     // griovelio sienelėje gylis
r = 3;              // kampų apvalinimas
hold_gap = 0.5;     // tarpas tarp dangčio stulpelio ir DAC plokštės (neleidžia DAC pakilti)
// DAC įstatomas įstrižai: lizdas į sienelės skylę, galinis kraštas žemyn – spragteli po dviem spaustukais.
// Guli ant atramų (galinė lentynėlė + dvi priekyje) – juostos po DAC nereikia.
dac_clip_t = 1.3;     // spaustuko storis (0.8 lūžo); lūžta – storink, per kietas – plonink
dac_clip_hook = 0.3;  // kiek kabliukas užeina ant DAC krašto; laikosi per silpnai – 0.5

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
  xe = wall + dac_pos[0] + dac[0];   // DAC galinis kraštas
  ys = wall + dac_pos[1];            // DAC galai išilgai sienelės
  clips = [ys + 8, ys + dac[1] - 8]; // spaustukų centrai
  union() {
    difference() {
      union() {
        difference() {
          rounded(outer, floor_t + inner_h);
          translate([wall, wall, floor_t]) rounded(inner, inner_h + 1, r - wall / 2);
        }
        intersection() {  // ribotuvai neišlenda pro sieneles (0.5 mm į jas įeina – kad susilietų)
          translate([wall - 0.5, wall - 0.5, 0]) cube([inner[0] + 1, inner[1] + 1, floor_t + inner_h]);
          union() {
            stops(esp_pos, esp, 0, esp_ant_w); stops(dac_pos, dac); stops(rtc_pos, rtc_size, rtc_pin_gap, rtc_pin_gap);
            // atrama už DAC galinio krašto iki plokštės viršaus (kišant kištuką DAC nepasislenka) ...
            translate([xe + 0.2, ys + 1, floor_t - 0.5]) cube([2, dac[1] - 2, z0 + pcb - 0.3 - floor_t + 0.5]);
            // ... ir lentynėlė po galiniu kraštu (neužlituotų kontaktų eilė – apačioje laisva)
            translate([xe - 1.5, ys + 1, floor_t - 0.5]) cube([1.8, dac[1] - 2, z0 - floor_t + 0.5]);
            // dvi atramėlės priekyje (ne po lizdu ir ne po 6 kontaktų eile)
            for (y = [ys + 14, ys + dac[1] - 9]) translate([wall + dac_pos[0] + 1, y, floor_t - 0.5]) cube([3, 3, z0 - floor_t + 0.5]);
          }
        }
      }
      // tarpai aplink spaustukus, kad lankstytųsi (dugne plyšio nėra – 0.6 mm plėvelė lūžo)
      for (y = clips) translate([xe - 1.6, y - 3.6, floor_t]) cube([dac_clip_t + 4, 7.2, inner_h]);
      // griovelis aplink sieneles, už kurio užsikabina dangčio dantukai
      translate([wall - groove_d, wall - groove_d, floor_t + inner_h - snap_l - 0.3])
        rounded([inner[0] + 2 * groove_d, inner[1] + 2 * groove_d], 3.1, r - wall / 2 + groove_d);
      // abi USB-C jungtys (kištuko korpusas ~12.5 x 7.5)
      translate([-1, wall + esp_pos[1] + usb_side, z0 + pcb + usb_h / 2 - 4.5]) cube([wall + 2, esp[1] - 2 * usb_side, 9 - usb_top]);
      // 3.5 mm lizdas
      translate([-1, wall + dac_pos[1] + jack_y, z0 + pcb + jack_z]) rotate([0, 90, 0]) cylinder(d = jack_d, h = wall + 2);
    }
    // DAC spaustukai: stovas nuo dugno + kabliukas su nuožulnia įėjimo puse
    zt = z0 + pcb;  // DAC plokštės viršus
    for (y = clips) {
      translate([xe + 0.15, y - 3, floor_t - 0.5]) cube([dac_clip_t, 6, zt + 2.15 - floor_t + 0.5]);
      translate([0, y + 3, 0]) rotate([90, 0, 0]) linear_extrude(6)
        polygon([[xe + 0.16, zt + 0.15], [xe - dac_clip_hook, zt + 0.15], [xe - dac_clip_hook, zt + 0.95], [xe + 0.16, zt + 2.15]]);
    }
  }
}

// dangčio stulpelio vieta dėžutės koordinatėmis: DAC galinis kraštas (neužlituotų kontaktų eilė), vidurys
dac_post = [wall + dac_pos[0] + dac[0] - 3.1, wall + dac_pos[1] + dac[1] / 2];

module lid() {
  lip = 4;
  lw = [inner[0] - 2 * fit, inner[1] - 2 * fit];  // lūpos išorė
  yc = outer[1] / 2;
  difference() {
    union() {
      rounded(outer, lid_t);
      // lūpa įeina į dėžutę; ties dantukais išpjauta, kad dantukas lankstytųsi
      translate([wall + fit, wall + fit, lid_t]) difference() {
        rounded(lw, lip, r - wall / 2);
        translate([1.6, 1.6, -1]) rounded([lw[0] - 3.2, lw[1] - 3.2], lip + 2, 1);
        translate([-1, yc - wall - fit - snap_w / 2 - 0.6, -1]) cube([lw[0] + 2, snap_w + 1.2, lip + 2]);
      }
      // stulpelis iki hold_gap virš DAC plokštės; dangtis apverstas per X ašį, tad y veidrodinis
      // 3.5 mm storio, ties dangčiu platėja iki 7 mm – ten jis lūžo
      translate([dac_post[0], outer[1] - dac_post[1], lid_t - 0.5]) {
        translate([-1.75, -1.75, 0]) cube([3.5, 3.5, floor_t + inner_h - (z0 + pcb + hold_gap) + 0.5]);
        hull() {
          translate([-3.5, -3.5, 0]) cube([7, 7, 0.5]);
          translate([-1.75, -1.75, 0]) cube([3.5, 3.5, 4.5]);
        }
      }
      // dantukai; kabliukas: nuožulni įėjimo pusė prie galiuko, 45° užkabinimas – nuimamas patraukus
      for (side = [0, 1]) {
        xo = side ? wall + fit + lw[0] : wall + fit;  // lūpos išorinis paviršius
        translate([side ? xo - snap_t : xo, yc - snap_w / 2, lid_t - 0.5]) cube([snap_t, snap_w, snap_l + 0.5]);
        translate([xo, yc + snap_w / 2, lid_t + snap_l]) mirror([side, 0, 0]) rotate([90, 0, 0])
          linear_extrude(snap_w) polygon([[0.01, 0], [-snap_hook, -1.3], [-snap_hook, -1.9], [0.01, -2.5]]);
      }
    }
    // "◄ USB" viršuje (spausdinant – apačioje): kuria puse dėti dangtį, kad stulpelis būtų virš DAC
    translate([outer[0] - 20, outer[1] / 2, -1]) linear_extrude(1.6) mirror([0, 1, 0]) {  // laisvoje vietoje už plyšių
      polygon([[0, 0], [4, -3], [4, 3]]);
      translate([5.5, 0]) text("USB", size = 5, halign = "left", valign = "center");
    }
    // ventiliacija virš ESP32 (stabilizatorius) ir DAC; antenos galo ir stulpelio nelieskit
    for (i = [0:5]) translate([wall + 20 + i * 6, wall + 5, -1]) cube([2.5, inner[1] - 10, lid_t + 2]);
  }
  assert(dac_post[0] + 3.5 < wall + 20, "dangčio stulpelis ant ventiliacijos plyšio");
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
