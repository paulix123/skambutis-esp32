"""Instrukcijos ekrano nuotraukos: programa prieš bandomąjį įrenginį (tools/mock_device.py).

    python app/screenshots.py <aplankas>      (Windows; GitHub Actions: docs-screenshots.yml)
"""
import ctypes
import ctypes.wintypes
import sys
import time
from pathlib import Path
from tkinter import messagebox

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))
ctypes.windll.shcore.SetProcessDpiAwareness(1)  # kaip programoje – kad koordinatės sutaptų su nuotrauka

import mock_device  # noqa: E402
import skambutis  # noqa: E402
from PIL import ImageGrab  # noqa: E402

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for name in ("showinfo", "showwarning", "showerror"):
    setattr(messagebox, name, lambda *a, **k: None)  # modalūs langai sustabdytų nuotraukų darymą
mock_device.serve(8099)

app = skambutis.App()
app.geometry("900x640+60+40")
app.attributes("-topmost", True)
app.ip_var.set("skambutis-esp (127.0.0.1:8099)")
app.pin_var.set("1234")


def pump(seconds=0.6, until=None, timeout=20):
    t = time.time()
    while time.time() - t < (timeout if until else seconds):
        app.update()
        time.sleep(0.02)
        if until and until():
            break


def shot(window, name):
    pump(0.8)
    r = ctypes.wintypes.RECT()
    hwnd = int(window.wm_frame(), 16)
    ctypes.windll.dwmapi.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(r), ctypes.sizeof(r))  # be šešėlio
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom)).save(out / f"{name}.png")
    print("nuotrauka:", name)


pump(1)
shot(app, "00-neprisijungta")
app.connect()
pump(until=lambda: app.cfg is not None and app.conn.get().startswith("Prisijungta"))
app.refresh_status()
pump(1.5)

app.nb.select(0)
app.lessons.selection_set("0")
app.on_lesson_select(None)
shot(app, "01-tvarkarastis")

app.nb.select(1)
app.sounds.selection_set("1")
app.on_sound_select(None)
shot(app, "02-garsai")

app.nb.select(2)
shot(app, "03-signalai")

app.nb.select(3)
shot(app, "04-irenginys")

app.nb.select(0)
app.start_var.set("16:10")
app.end_var.set("16:55")
app.lesson_add()
shot(app, "05-neissaugota")
app.revert()

before = set(app.winfo_children())
app.show_log()
pump(until=lambda: any(w.winfo_class() == "Toplevel" for w in app.winfo_children() if w not in before))
log = next(w for w in app.winfo_children() if w.winfo_class() == "Toplevel" and w not in before)
log.geometry("+120+90")
log.attributes("-topmost", True)
shot(log, "06-zurnalas")
app.destroy()
print("OK")
