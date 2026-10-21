"""Mokyklos skambučio valdymas: tvarkaraštis, garsai, signalai. Windows: dukart spustelėti Skambutis.exe.

Kūrimui: uv run --with zeroconf --with miniaudio --with lameenc --with numpy --with requests app/skambutis.py
"""
import copy
import queue
import re
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk

import core

DAYS = ["Pr", "An", "Tr", "Kt", "Pn", "Št", "Sk"]
HHMM = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")
LABEL_USE = {v: k for k, v in core.USES.items()}
ALERT_MODES = {"off": "Išjungta", "log": "Tik įrašyti į žurnalą (negroti)", "play": "Groti signalą automatiškai"}
LABEL_ALERT = {v: k for k, v in ALERT_MODES.items()}
SIGNAL_COLORS = {"BALTAS": ("#f4f4f4", "#111"), "GELTONAS": ("#f5c518", "#111"), "RAUDONAS": ("#d93030", "#fff")}


def norm_time(s: str) -> str | None:
    """'8:5' / '08.05' / '0805' -> '08:05'; bloga reikšmė -> None."""
    s = s.strip().replace(".", ":")
    if re.fullmatch(r"\d{3,4}", s):
        s = s[:-2] + ":" + s[-2:]
    m = re.fullmatch(r"(\d{1,2}):(\d{1,2})", s)
    if not m:
        return None
    t = f"{int(m[1]):02d}:{int(m[2]):02d}"
    return t if HHMM.match(t) else None


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Mokyklos skambutis")
        self.geometry("860x620")
        self.minsize(760, 540)
        self.dev: core.Device | None = None
        self.cfg: dict | None = None
        self.saved: dict | None = None
        self.sizes: dict[str, int] = {}
        self.settings = core.load_settings()
        self.loading = False  # kol pildom laukus iš config – jų pakeitimai nėra vartotojo
        self.calls = queue.Queue()  # fono gijos -> pagrindinė (Tkinter nėra saugus gijoms)
        self.after(50, self.run_calls)
        self._build()
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.after(200, self.autoconnect)
        self.after(10000, self.refresh_status)

    # --- bendri ---------------------------------------------------------------

    def run_calls(self):
        while not self.calls.empty():
            fn, args = self.calls.get_nowait()
            fn(*args)
        self.after(50, self.run_calls)

    def bg(self, work, done=None, busy="Dirbama…"):
        """Tinklo darbas fone, kad langas nestrigtų; klaida – pranešimas."""
        self.msg.set(busy)
        self.configure(cursor="watch")

        def run():
            try:
                res, err = work(), None
            except Exception as e:  # rodom vartotojui bet kokią klaidą
                res, err = None, e
            self.calls.put((finish, (res, err)))

        def finish(res, err):
            self.configure(cursor="")
            self.msg.set("")
            if err:
                messagebox.showerror("Klaida", str(err))
            elif done:
                done(res)

        threading.Thread(target=run, daemon=True).start()

    def need_device(self) -> bool:
        if self.dev and self.cfg is not None:
            return True
        messagebox.showinfo("Neprisijungta", "Pirma prisijunk prie įrenginio (viršuje).")
        return False

    def mark_dirty(self):
        dirty = self.cfg is not None and self.cfg != self.saved
        self.save_btn.state(["!disabled"] if dirty else ["disabled"])
        self.title(("* " if dirty else "") + "Mokyklos skambutis")

    # --- langas ---------------------------------------------------------------

    def _build(self):
        top = ttk.Frame(self, padding=(10, 8))
        top.pack(fill="x")
        ttk.Label(top, text="Įrenginys:").pack(side="left")
        self.ip_var = tk.StringVar(value=self.settings.get("ip", ""))
        self.ip_box = ttk.Combobox(top, textvariable=self.ip_var, width=30)
        self.ip_box.pack(side="left", padx=4)
        ttk.Button(top, text="Ieškoti tinkle", command=self.discover).pack(side="left")
        ttk.Label(top, text="PIN:").pack(side="left", padx=(12, 2))
        self.pin_var = tk.StringVar(value=self.settings.get("pin", ""))
        pin_entry = ttk.Entry(top, textvariable=self.pin_var, show="•", width=10)
        pin_entry.pack(side="left")
        pin_entry.bind("<Return>", lambda e: self.connect())
        ttk.Button(top, text="Prisijungti", command=self.connect).pack(side="left", padx=4)
        self.conn = tk.StringVar(value="Neprisijungta")
        self.conn_lbl = ttk.Label(top, textvariable=self.conn, foreground="#b00")
        self.conn_lbl.pack(side="left", padx=8)

        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=10)
        self.nb = nb
        nb.add(self._tab_schedule(nb), text="Tvarkaraštis")
        nb.add(self._tab_sounds(nb), text="Garsai")
        nb.add(self._tab_signals(nb), text="Signalai ir bandymas")
        nb.add(self._tab_device(nb), text="Įrenginys")

        bottom = ttk.Frame(self, padding=(10, 8))
        bottom.pack(fill="x")
        self.save_btn = ttk.Button(bottom, text="Išsaugoti įrenginyje", command=self.save)
        self.save_btn.pack(side="left")
        self.save_btn.state(["disabled"])
        ttk.Button(bottom, text="Atšaukti pakeitimus", command=self.revert).pack(side="left", padx=6)
        self.msg = tk.StringVar()
        ttk.Label(bottom, textvariable=self.msg, foreground="#555").pack(side="left", padx=10)

    def _tab_schedule(self, parent):
        f = ttk.Frame(parent, padding=10)
        left = ttk.Frame(f)
        left.pack(side="left", fill="y")
        self.lessons = ttk.Treeview(left, columns=("nr", "start", "end"), show="headings", height=14, selectmode="browse")
        for col, txt, w in (("nr", "Pamoka", 70), ("start", "Pradžia", 90), ("end", "Pabaiga", 90)):
            self.lessons.heading(col, text=txt)
            self.lessons.column(col, width=w, anchor="center")
        self.lessons.pack(side="left", fill="y")
        sb = ttk.Scrollbar(left, orient="vertical", command=self.lessons.yview)
        sb.pack(side="left", fill="y")
        self.lessons.configure(yscrollcommand=sb.set)
        self.lessons.bind("<<TreeviewSelect>>", self.on_lesson_select)

        right = ttk.Frame(f, padding=(16, 0))
        right.pack(side="left", fill="both", expand=True)
        edit = ttk.LabelFrame(right, text="Pamoka", padding=10)
        edit.pack(fill="x")
        self.start_var, self.end_var = tk.StringVar(), tk.StringVar()
        ttk.Label(edit, text="Pradžia").grid(row=0, column=0, sticky="w")
        ttk.Entry(edit, textvariable=self.start_var, width=8).grid(row=0, column=1, padx=6)
        ttk.Label(edit, text="Pabaiga").grid(row=0, column=2, sticky="w")
        ttk.Entry(edit, textvariable=self.end_var, width=8).grid(row=0, column=3, padx=6)
        btns = ttk.Frame(edit)
        btns.grid(row=1, column=0, columnspan=4, sticky="w", pady=(8, 0))
        ttk.Button(btns, text="Pridėti", command=self.lesson_add).pack(side="left")
        ttk.Button(btns, text="Pakeisti pasirinktą", command=self.lesson_update).pack(side="left", padx=4)
        ttk.Button(btns, text="Pašalinti pasirinktą", command=self.lesson_delete).pack(side="left")
        ttk.Label(edit, text="Laikas – HH:MM, pvz. 08:30", foreground="#666").grid(row=2, column=0, columnspan=4, sticky="w", pady=(6, 0))

        opts = ttk.LabelFrame(right, text="Skambinimas", padding=10)
        opts.pack(fill="x", pady=12)
        self.pre_var = tk.IntVar(value=2)
        row = ttk.Frame(opts)
        row.pack(anchor="w")
        ttk.Label(row, text="Skambinti prieš pamoką:").pack(side="left")
        ttk.Spinbox(row, from_=0, to=30, width=4, textvariable=self.pre_var, command=self.opts_changed).pack(side="left", padx=4)
        ttk.Label(row, text="min (0 – neskambinti)").pack(side="left")
        self.end_on = tk.BooleanVar(value=True)
        ttk.Checkbutton(opts, text="Skambinti ir pamokos pabaigoje", variable=self.end_on, command=self.opts_changed).pack(anchor="w", pady=6)
        days = ttk.Frame(opts)
        days.pack(anchor="w")
        ttk.Label(days, text="Dienos:").pack(side="left")
        self.day_vars = [tk.BooleanVar() for _ in DAYS]
        for name, v in zip(DAYS, self.day_vars):
            ttk.Checkbutton(days, text=name, variable=v, command=self.opts_changed).pack(side="left", padx=2)
        vol = ttk.Frame(opts)
        vol.pack(anchor="w", pady=(8, 0))
        ttk.Label(vol, text="Garsumas:").pack(side="left")
        self.vol_var = tk.IntVar(value=21)
        ttk.Scale(vol, from_=0, to=21, variable=self.vol_var, length=200,
                  command=lambda v: (self.vol_var.set(round(float(v))), self.opts_changed())).pack(side="left", padx=4)
        ttk.Label(vol, textvariable=self.vol_var, width=3).pack(side="left")
        ttk.Label(vol, text="iš 21", foreground="#666").pack(side="left")
        for v in (self.pre_var,):
            v.trace_add("write", lambda *a: self.opts_changed())
        return f

    def _tab_sounds(self, parent):
        f = ttk.Frame(parent, padding=10)
        self.sounds = ttk.Treeview(f, columns=("name", "use", "size"), show="headings", height=12, selectmode="browse")
        for col, txt, w in (("name", "Pavadinimas", 300), ("use", "Paskirtis", 220), ("size", "Dydis", 80)):
            self.sounds.heading(col, text=txt)
            self.sounds.column(col, width=w, anchor="w" if col != "size" else "e")
        self.sounds.pack(fill="x")
        self.sounds.bind("<<TreeviewSelect>>", self.on_sound_select)
        row = ttk.Frame(f)
        row.pack(fill="x", pady=8)
        ttk.Button(row, text="Įkelti garsą…", command=self.sound_upload).pack(side="left")
        ttk.Button(row, text="▶ Groti per įrenginį", command=self.sound_play).pack(side="left", padx=4)
        ttk.Button(row, text="Pervadinti…", command=self.sound_rename).pack(side="left")
        ttk.Button(row, text="Pašalinti", command=self.sound_delete).pack(side="left", padx=4)
        ttk.Label(row, text="Paskirtis:").pack(side="left", padx=(16, 4))
        self.use_var = tk.StringVar()
        use_box = ttk.Combobox(row, textvariable=self.use_var, values=list(core.USES.values()), state="readonly", width=26)
        use_box.pack(side="left")
        use_box.bind("<<ComboboxSelected>>", self.sound_set_use)
        ttk.Label(f, foreground="#555", justify="left", wraplength=780, text=(
            "• Kelių garsų ta pačia paskirtimi – kiekvieną kartą parenkamas atsitiktinis.\n"
            "• Jei „Prieš pamoką“ garso nėra – skamba pamokos pradžios garsas.\n"
            "• Įkeliamas garsas paruošiamas automatiškai: MP3, vienodas garsumas, nukirpta tyla. Tinka mp3, wav, flac, ogg.\n"
            "• Kiekvienam signalui (BALTAS, GELTONAS, RAUDONAS) – vienas garsas.")).pack(anchor="w")
        self.space = tk.StringVar()
        ttk.Label(f, textvariable=self.space, foreground="#555").pack(anchor="w", pady=(8, 0))
        return f

    def _tab_signals(self, parent):
        f = ttk.Frame(parent, padding=16)
        ttk.Label(f, text="Bandymas", font=("TkDefaultFont", 11, "bold")).pack(anchor="w")
        row = ttk.Frame(f)
        row.pack(anchor="w", pady=(4, 18))
        ttk.Button(row, text="Bandomasis skambutis", command=lambda: self.play("bendras")).pack(side="left")
        ttk.Button(row, text="Prieš pamoką", command=lambda: self.play("pries")).pack(side="left", padx=6)
        ttk.Button(row, text="■ Sustabdyti", command=self.stop).pack(side="left")
        ttk.Label(f, text="Civilinės saugos signalai", font=("TkDefaultFont", 11, "bold")).pack(anchor="w")
        ttk.Label(f, text="Groja iš karto per visus garsiakalbius – prieš grojant paklausiama.", foreground="#555").pack(anchor="w")
        row = ttk.Frame(f)
        row.pack(anchor="w", pady=10)
        for name, (bgc, fg) in SIGNAL_COLORS.items():
            tk.Button(row, text=name, bg=bgc, fg=fg, activebackground=bgc, activeforeground=fg, font=("TkDefaultFont", 13, "bold"),
                      width=12, height=2, relief="raised", bd=2, command=lambda n=name: self.signal(n)).pack(side="left", padx=(0, 10))
        ttk.Button(f, text="■ Sustabdyti", command=self.stop).pack(anchor="w")

        box = ttk.LabelFrame(f, text="Automatiniai oro pavojaus perspėjimai (lt72.lt)", padding=10)
        box.pack(fill="x", pady=(18, 0))
        row = ttk.Frame(box)
        row.pack(anchor="w")
        ttk.Label(row, text="Veiksmas:").pack(side="left")
        self.alerts_var = tk.StringVar(value=ALERT_MODES["off"])
        mode = ttk.Combobox(row, textvariable=self.alerts_var, values=list(ALERT_MODES.values()), state="readonly", width=32)
        mode.pack(side="left", padx=4)
        mode.bind("<<ComboboxSelected>>", self.alerts_changed)
        ttk.Label(row, text="Vietovė:").pack(side="left", padx=(16, 4))
        self.place_var = tk.StringVar(value="Vilniaus")
        ttk.Entry(row, textvariable=self.place_var, width=14).pack(side="left")
        self.place_var.trace_add("write", lambda *a: self.opts_changed())
        self.alert_info = tk.StringVar(value="–")
        ttk.Label(box, textvariable=self.alert_info).pack(anchor="w", pady=(8, 0))
        ttk.Label(box, foreground="#555", justify="left", wraplength=780, text=(
            "Kai kariuomenė paskelbia oro pavojų (GELTONA, RAUDONA) ar jį atšaukia (BALTA) mokyklos vietovei, įrenginys per ~1 min "
            "sugroja tą patį signalą. Vietovė – kilmininku, kaip pranešimuose: „Vilniaus“ tinka Vilniaus apskričiai ir miestui, "
            "bet ne Vilniaus rajonui. Tai papildomas perspėjimas – ne vietoj sirenų ir pranešimų telefonuose.")).pack(anchor="w", pady=(4, 0))
        return f

    def alerts_changed(self, _):
        if LABEL_ALERT[self.alerts_var.get()] == "play" and not messagebox.askyesno(
                "Automatiniai perspėjimai", "Oro pavojaus signalai skambės per visus garsiakalbius automatiškai, be žmogaus "
                "patvirtinimo. Ar tai suderinta su mokyklos civilinės saugos planu?", icon="warning"):
            self.alerts_var.set(ALERT_MODES[(self.cfg or {}).get("alerts", "off")])
            return
        self.opts_changed()

    def _tab_device(self, parent):
        f = ttk.Frame(parent, padding=16)
        self.dev_info = {}
        grid = ttk.Frame(f)
        grid.pack(anchor="w")
        for i, (key, txt) in enumerate((("time", "Laikas įrenginyje"), ("next", "Sekantis skambutis"), ("wifi", "WiFi"),
                                        ("rtc", "Laikrodis (DS3231)"), ("fs", "Vieta garsams"), ("version", "Programos versija"),
                                        ("update", "Atnaujinimai"))):
            ttk.Label(grid, text=txt + ":").grid(row=i, column=0, sticky="w", pady=2)
            v = tk.StringVar(value="–")
            ttk.Label(grid, textvariable=v).grid(row=i, column=1, sticky="w", padx=10)
            self.dev_info[key] = v
        row = ttk.Frame(f)
        row.pack(anchor="w", pady=16)
        ttk.Button(row, text="Ieškoti atnaujinimų", command=self.check_update).pack(side="left", padx=(0, 6))
        ttk.Button(row, text="Žurnalas…", command=self.show_log).pack(side="left")
        ttk.Button(row, text="Keisti PIN…", command=self.change_pin).pack(side="left", padx=6)
        ttk.Button(row, text="Atsarginė kopija…", command=self.backup).pack(side="left")
        ttk.Button(row, text="Atkurti iš kopijos…", command=self.restore).pack(side="left", padx=6)
        lead = ttk.Frame(f)
        lead.pack(anchor="w")
        ttk.Label(lead, text="Paleisti garsą anksčiau:").pack(side="left")
        self.lead_var = tk.IntVar(value=200)
        ttk.Spinbox(lead, from_=0, to=2000, increment=50, width=6, textvariable=self.lead_var, command=self.opts_changed).pack(side="left", padx=4)
        ttk.Label(lead, text="ms – kad skambutis pasigirstų tiksliai minutės pradžioje (numatyta 200)", foreground="#555").pack(side="left")
        self.lead_var.trace_add("write", lambda *a: self.opts_changed())
        return f

    # --- prisijungimas --------------------------------------------------------

    def autoconnect(self):
        if self.ip_var.get() and self.pin_var.get():
            self.connect()
        else:
            self.discover()

    def discover(self):
        def done(found):
            self.ip_box["values"] = [f"{n} ({ip})" for n, ip in found]
            if found and not self.ip_var.get():
                self.ip_box.current(0)
            if not found:
                messagebox.showinfo("Nerasta", "Įrenginys tinkle nerastas. Patikrink, ar kompiuteris tame pačiame tinkle, "
                                               "arba įvesk IP adresą ranka.")
        self.bg(lambda: core.find_devices(3), done, "Ieškoma tinkle…")

    def host(self) -> str:
        t = self.ip_var.get().strip()
        m = re.search(r"\(([^)]+)\)$", t)
        return m[1] if m else t

    def connect(self):
        host, pin = self.host(), self.pin_var.get().strip()
        if not host or not pin:
            messagebox.showinfo("Prisijungimas", "Įvesk įrenginio adresą ir PIN.")
            return
        dev = core.Device(host, pin)

        def done(res):
            st, cfg, files = res
            self.dev, self.cfg, self.saved = dev, cfg, copy.deepcopy(cfg)
            self.sizes = {f["file"]: f["size"] for f in files}
            self.settings.update(ip=host, pin=pin)
            core.save_settings(self.settings)
            self.fill_all()
            self.show_status(st)
        self.bg(lambda: (dev.status(), dev.config(), dev.files()), done, "Jungiamasi…")

    def refresh_status(self):
        dev = self.dev

        def run():
            try:
                self.calls.put((self.show_status, (dev.status(),)))
            except core.DeviceError:
                self.calls.put((lambda: (self.conn.set("Ryšys prarastas"), self.conn_lbl.configure(foreground="#b00")), ()))
        if dev:
            threading.Thread(target=run, daemon=True).start()
        self.after(10000, self.refresh_status)

    def show_status(self, st):
        self.conn.set(f"Prisijungta · {st.get('time', 'laikas nežinomas')}")
        self.conn_lbl.configure(foreground="#070")
        self.dev_info["time"].set(st.get("time", "nesinchronizuotas – skambučiai neveikia"))
        self.dev_info["next"].set(st.get("next_bell", "šiandien daugiau nėra"))
        w = st["wifi"]
        self.dev_info["wifi"].set(f"{w['ssid']}, {w['rssi']} dBm, {w['ip']}")
        r = st["rtc"]
        if not r["ok"]:
            rtc = "nerastas"
        elif "temp" not in r:
            rtc = "neatsako (laidai SDA 8, SCL 18?)"
        else:
            rtc = f"veikia, {r['temp']:.1f} °C" + (" – laikas buvo sustojęs (baterija?)" if r.get("lost") else "")
        self.dev_info["rtc"].set(rtc)
        fs = st["fs"]
        self.dev_info["fs"].set(f"užimta {fs['used'] / 1e6:.1f} iš {fs['total'] / 1e6:.1f} MB")
        self.dev_info["version"].set(st["version"])
        self.dev_info["update"].set(st.get("update", "–"))
        a = st.get("alerts")
        if a:
            self.alert_info.set(f"Paskutinis patikrinimas: {a['info']}   ·   Paskutinis pranešimas: {a.get('last') or '–'}")
        self.space.set(f"Įrenginyje užimta {fs['used'] / 1e6:.1f} iš {fs['total'] / 1e6:.1f} MB")

    # --- config -> laukai ---------------------------------------------------------

    def fill_all(self):
        self.loading = True
        c = self.cfg
        self.pre_var.set(c.get("pre_minutes", 2))
        self.end_on.set(c.get("ring_end", True))
        for i, v in enumerate(self.day_vars):
            v.set(i in c.get("days", []))
        self.vol_var.set(c.get("volume", 21))
        self.lead_var.set(c.get("lead_ms", 200))
        self.alerts_var.set(ALERT_MODES.get(c.get("alerts", "off"), ALERT_MODES["off"]))
        self.place_var.set(c.get("alert_place", "Vilniaus"))
        self.loading = False
        self.fill_lessons()
        self.fill_sounds()
        self.mark_dirty()

    def fill_lessons(self, select: int | None = None):
        self.cfg["lessons"].sort(key=lambda l: l["start"])
        self.lessons.delete(*self.lessons.get_children())
        for i, l in enumerate(self.cfg["lessons"]):
            self.lessons.insert("", "end", iid=str(i), values=(i + 1, l["start"], l["end"]))
        if select is not None and 0 <= select < len(self.cfg["lessons"]):
            self.lessons.selection_set(str(select))

    def fill_sounds(self):
        self.sounds.delete(*self.sounds.get_children())
        for i, s in enumerate(self.cfg["sounds"]):
            size = self.sizes.get(s["file"])
            self.sounds.insert("", "end", iid=str(i), values=(s["name"], core.USES.get(s["use"], s["use"]),
                                                              f"{size // 1024} KB" if size else ""))

    def opts_changed(self):
        if self.loading or self.cfg is None:
            return
        try:
            self.cfg["pre_minutes"] = max(0, min(30, int(self.pre_var.get())))
            self.cfg["lead_ms"] = max(0, min(2000, int(self.lead_var.get())))
        except (tk.TclError, ValueError):
            return  # vartotojas dar rašo skaičių
        self.cfg["ring_end"] = self.end_on.get()
        self.cfg["days"] = [i for i, v in enumerate(self.day_vars) if v.get()]
        self.cfg["volume"] = int(self.vol_var.get())
        self.cfg["alerts"] = LABEL_ALERT[self.alerts_var.get()]
        self.cfg["alert_place"] = self.place_var.get().strip()
        self.mark_dirty()

    # --- pamokos ----------------------------------------------------------------

    def selected(self, tree) -> int | None:
        sel = tree.selection()
        return int(sel[0]) if sel else None

    def on_lesson_select(self, _):
        i = self.selected(self.lessons)
        if i is not None:
            l = self.cfg["lessons"][i]
            self.start_var.set(l["start"])
            self.end_var.set(l["end"])

    def lesson_values(self):
        a, b = norm_time(self.start_var.get()), norm_time(self.end_var.get())
        if not a or not b:
            messagebox.showwarning("Blogas laikas", "Įvesk laiką HH:MM formatu, pvz. 08:30.")
            return None
        if b <= a:
            messagebox.showwarning("Blogas laikas", "Pamokos pabaiga turi būti vėliau už pradžią.")
            return None
        return {"start": a, "end": b}

    def sorted_index(self, l) -> int:
        self.cfg["lessons"].sort(key=lambda x: x["start"])
        return self.cfg["lessons"].index(l)

    def lesson_add(self):
        if not self.need_device() or not (l := self.lesson_values()):
            return
        self.cfg["lessons"].append(l)
        self.fill_lessons(select=self.sorted_index(l))
        self.mark_dirty()

    def lesson_update(self):
        i = self.selected(self.lessons)
        if i is None:
            messagebox.showinfo("Pamoka", "Pasirink pamoką sąraše.")
            return
        if not (l := self.lesson_values()):
            return
        self.cfg["lessons"][i] = l
        self.fill_lessons(select=self.sorted_index(l))
        self.mark_dirty()

    def lesson_delete(self):
        i = self.selected(self.lessons)
        if i is None:
            return
        del self.cfg["lessons"][i]
        self.fill_lessons()
        self.mark_dirty()

    # --- garsai -----------------------------------------------------------------

    def on_sound_select(self, _):
        i = self.selected(self.sounds)
        if i is not None:
            self.use_var.set(core.USES.get(self.cfg["sounds"][i]["use"], ""))

    def sound_set_use(self, _):
        i = self.selected(self.sounds)
        if i is None:
            return
        use = LABEL_USE[self.use_var.get()]
        if use in core.SIGNALS:  # signalui – tik vienas garsas
            for s in self.cfg["sounds"]:
                if s["use"] == use:
                    s["use"] = "off"
        self.cfg["sounds"][i]["use"] = use
        self.fill_sounds()
        self.sounds.selection_set(str(i))
        self.mark_dirty()

    def sound_upload(self):
        if not self.need_device():
            return
        paths = filedialog.askopenfilenames(title="Pasirink garso failus",
                                            filetypes=[("Garsai", "*.mp3 *.wav *.flac *.ogg"), ("Visi failai", "*.*")])
        if not paths:
            return
        dev = self.dev

        def work():
            added = []
            for p in paths:
                mp3, dur = core.prepare_sound(p)
                added.append((Path(p).stem, dev.upload(mp3, Path(p).stem + ".mp3"), len(mp3), dur))
            return added

        def done(added):
            has_bell = any(s["use"] == "bendras" for s in self.cfg["sounds"])
            for name, file, size, dur in added:
                self.cfg["sounds"].append({"file": file, "name": name, "use": "off" if has_bell else "bendras"})
                self.sizes[file] = size
            self.fill_sounds()
            self.sounds.selection_set(str(len(self.cfg["sounds"]) - 1))
            self.mark_dirty()
            messagebox.showinfo("Įkelta", f"Įkelta garsų: {len(added)}.\nPasirink jų paskirtį ir spausk „Išsaugoti įrenginyje“.")
        self.bg(work, done, "Ruošiamas ir įkeliamas garsas…")

    def sound_play(self):
        i = self.selected(self.sounds)
        if i is None or not self.need_device():
            return
        file = self.cfg["sounds"][i]["file"]
        self.bg(lambda: self.dev.play(file=file), busy="Groja…")

    def sound_rename(self):
        i = self.selected(self.sounds)
        if i is None:
            return
        name = simpledialog.askstring("Pervadinti", "Naujas pavadinimas:", initialvalue=self.cfg["sounds"][i]["name"], parent=self)
        if name and name.strip():
            self.cfg["sounds"][i]["name"] = name.strip()
            self.fill_sounds()
            self.mark_dirty()

    def sound_delete(self):
        i = self.selected(self.sounds)
        if i is None:
            return
        s = self.cfg["sounds"][i]
        if s["use"] == "bendras" and sum(x["use"] == "bendras" for x in self.cfg["sounds"]) == 1:
            if not messagebox.askyesno("Pašalinti", "Tai vienintelis pamokos skambučio garsas – be jo skambučiai tylės. Vis tiek pašalinti?"):
                return
        del self.cfg["sounds"][i]
        self.fill_sounds()
        self.mark_dirty()

    # --- grojimas ---------------------------------------------------------------

    def play(self, what):
        if self.need_device():
            self.bg(lambda: self.dev.play(what=what), busy="Groja…")

    def signal(self, name):
        if self.need_device() and messagebox.askyesno("Signalas", f"Groti {name} signalą per visus garsiakalbius?", icon="warning"):
            self.play(name)

    def stop(self):
        if self.dev:
            self.bg(self.dev.stop, busy="Stabdoma…")

    # --- išsaugojimas -----------------------------------------------------------

    def save(self):
        if not self.need_device():
            return
        if not self.cfg["lessons"]:
            if not messagebox.askyesno("Išsaugoti", "Tvarkaraštyje nėra pamokų – skambučiai neskambės. Išsaugoti?"):
                return
        if not any(s["use"] == "bendras" for s in self.cfg["sounds"]):
            if not messagebox.askyesno("Išsaugoti", "Nėra garso „Pamokos pradžia ir pabaiga“ – skambučiai tylės. Išsaugoti?"):
                return
        cfg = copy.deepcopy(self.cfg)

        def done(files):
            self.saved = cfg
            self.sizes = {f["file"]: f["size"] for f in files}
            self.mark_dirty()
            self.msg.set("Išsaugota įrenginyje")
            self.after(4000, lambda: self.msg.set(""))
        self.bg(lambda: (self.dev.save_config(cfg), self.dev.files())[1], done, "Saugoma…")

    def revert(self):
        if self.saved is not None and self.cfg != self.saved:
            self.cfg = copy.deepcopy(self.saved)
            self.fill_all()

    def on_close(self):
        if self.cfg is not None and self.cfg != self.saved:
            ans = messagebox.askyesnocancel("Neišsaugota", "Yra neišsaugotų pakeitimų. Išsaugoti prieš uždarant?")
            if ans is None:
                return
            if ans:
                try:
                    self.dev.save_config(self.cfg)
                except core.DeviceError as e:
                    messagebox.showerror("Klaida", str(e))
                    return
        self.destroy()

    # --- įrenginys ----------------------------------------------------------------

    def show_log(self):
        if not self.need_device():
            return

        def done(text):
            w = tk.Toplevel(self)
            w.title("Įrenginio žurnalas")
            w.geometry("760x420")
            t = tk.Text(w, wrap="none", font=("Consolas", 10) if sys.platform == "win32" else ("Menlo", 11))
            t.insert("1.0", text or "(tuščias)")
            t.configure(state="disabled")
            t.pack(fill="both", expand=True)
            t.see("end")
        self.bg(self.dev.log, done, "Gaunamas žurnalas…")

    def check_update(self):
        if not self.need_device():
            return

        def done(_):
            self.dev_info["update"].set("tikrinama…")
            messagebox.showinfo("Atnaujinimai", "Įrenginys tikrina, ar yra nauja versija. Jei yra – įsidiegs ir persikraus "
                                                "(~1 min). Rezultatą matysi eilutėje „Atnaujinimai“.")
        self.bg(self.dev.check_update, done)

    def change_pin(self):
        if not self.need_device():
            return
        p = simpledialog.askstring("Keisti PIN", "Naujas PIN (4–12 skaitmenų):", parent=self)
        if not p:
            return
        if not re.fullmatch(r"\d{4,12}", p):
            messagebox.showwarning("PIN", "PIN – nuo 4 iki 12 skaitmenų.")
            return

        def done(_):
            self.pin_var.set(p)
            self.settings["pin"] = p
            core.save_settings(self.settings)
            messagebox.showinfo("PIN", "PIN pakeistas.")
        self.bg(lambda: self.dev.set_pin(p), done)

    def backup(self):
        if not self.need_device():
            return
        path = filedialog.asksaveasfilename(title="Atsarginė kopija", defaultextension=".zip", initialfile="skambutis-kopija.zip",
                                            filetypes=[("Zip", "*.zip")])
        if path:
            self.bg(lambda: core.export_backup(self.dev, path), lambda _: messagebox.showinfo("Kopija", f"Išsaugota:\n{path}"),
                    "Kuriama kopija…")

    def restore(self):
        if not self.need_device():
            return
        path = filedialog.askopenfilename(title="Atkurti iš kopijos", filetypes=[("Zip", "*.zip")])
        if not path or not messagebox.askyesno("Atkurti", "Įrenginio tvarkaraštis ir garsai bus pakeisti kopijos turiniu. Tęsti?"):
            return
        self.bg(lambda: core.import_backup(self.dev, path), lambda _: self.connect(), "Atkuriama…")


def selftest(out: str):
    """CI: ar į .exe pateko visos bibliotekos – garso paruošimas, paieška, langas."""
    import tempfile
    import wave

    import numpy as np
    import zeroconf  # noqa: F401 – importuojamas tik paieškoje, tad tikrinam atskirai

    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.wav"
        with wave.open(str(p), "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(48000)
            f.writeframes((np.sin(np.arange(48000) / 8) * 8000).astype(np.int16).tobytes())
        mp3, _ = core.prepare_sound(p)
    app = App()
    app.update()
    app.destroy()
    Path(out).write_text(f"OK {len(mp3)}", encoding="utf-8")


def main():
    if "--selftest" in sys.argv:
        selftest(sys.argv[-1])
        return
    if sys.platform == "win32":
        try:  # ryškus tekstas didelės raiškos ekranuose
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
    App().mainloop()


if __name__ == "__main__":
    main()
