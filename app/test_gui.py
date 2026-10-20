"""Lango patikrinimas be pelės: prisijungia, paspaudžia mygtukus kodu, nufotografuoja korteles.

    uv run --with zeroconf --with miniaudio --with lameenc --with numpy --with requests app/test_gui.py IP PIN [aplankas]
Įrenginyje nieko neišsaugo (tik atšaukiami pakeitimai)."""
import subprocess
import sys
import time
from pathlib import Path
from tkinter import messagebox

sys.path.insert(0, str(Path(__file__).parent))
import skambutis  # noqa: E402

ip, pin = sys.argv[1], sys.argv[2]
shots = Path(sys.argv[3]) if len(sys.argv) > 3 else None
messagebox.showinfo = lambda *a, **k: print("  info:", a[1] if len(a) > 1 else a)
messagebox.showwarning = lambda *a, **k: print("  įspėjimas:", a[1] if len(a) > 1 else a)
messagebox.showerror = lambda *a, **k: (_ for _ in ()).throw(AssertionError(f"klaida lange: {a}"))
messagebox.askyesno = lambda *a, **k: True

app = skambutis.App()
app.after_cancel  # noqa
app.ip_var.set(ip)
app.pin_var.set(pin)
app.connect()


def pump(cond, timeout=15):
    t = time.time()
    while not cond():
        app.update()
        assert time.time() - t < timeout, "laukimas baigėsi"
        time.sleep(0.02)


def dirty():
    return "disabled" not in app.save_btn.state()


pump(lambda: app.cfg is not None and app.conn.get().startswith("Prisijungta"))
n = len(app.cfg["lessons"])
assert len(app.lessons.get_children()) == n
print("prisijungta:", app.conn.get(), f"– {n} pamokų, {len(app.cfg['sounds'])} garsų")
assert not dirty()

app.start_var.set("17.0"); app.end_var.set("1745"); app.lesson_add()
assert len(app.cfg["lessons"]) == n + 1 and {"start": "17:00", "end": "17:45"} in app.cfg["lessons"] and dirty()
print("pamoka pridėta (17.0 / 1745 -> 17:00–17:45), mygtukas „Išsaugoti“ įjungtas")
app.start_var.set("10:00"); app.end_var.set("09:00"); app.lesson_add()
assert len(app.cfg["lessons"]) == n + 1, "bloga pamoka priimta"
app.revert()
assert len(app.cfg["lessons"]) == n and not dirty()
print("atšaukta – grįžo pradinis")

app.pre_var.set(5); app.update()
assert app.cfg["pre_minutes"] == 5 and dirty()
app.day_vars[5].set(True); app.opts_changed()
assert 5 in app.cfg["days"]
app.revert()
assert not dirty()
print("nustatymai keičiasi ir atšaukiami")

app.sounds.selection_set("0"); app.update()
app.use_var.set("Signalas BALTAS"); app.sound_set_use(None)
app.sounds.selection_set("1"); app.update()
app.use_var.set("Signalas BALTAS"); app.sound_set_use(None)
uses = [s["use"] for s in app.cfg["sounds"]]
assert uses.count("BALTAS") == 1 and uses[1] == "BALTAS", uses
print("signalui lieka tik vienas garsas:", uses)
app.revert()

app.refresh_status()
pump(lambda: app.dev_info["version"].get() != "–")
print("įrenginio kortelė:", {k: v.get() for k, v in app.dev_info.items()})

if shots:
    shots.mkdir(parents=True, exist_ok=True)
    app.geometry("+40+60"); app.lift(); app.attributes("-topmost", True)
    for i, name in enumerate(["tvarkarastis", "garsai", "signalai", "irenginys"]):
        app.nb.select(i)
        for _ in range(20):
            app.update(); time.sleep(0.03)
        x, y, w, h = app.winfo_rootx(), app.winfo_rooty(), app.winfo_width(), app.winfo_height()
        subprocess.run(["screencapture", "-x", f"-R{x},{y - 28},{w},{h + 28}", str(shots / f"{name}.png")])
    print("nuotraukos:", shots)
app.destroy()
print("OK")
