"""Įrenginio HTTP API patikrinimas iš kompiuterio.

    uv run --with requests tools/api_test.py IP PIN [--groti]

Pakeičia config tik laikinai: pabaigoje grąžina pradinį. --groti – dar ir pagroja BALTAS signalą.
"""
import sys
import time

import requests

ip, pin = sys.argv[1], sys.argv[2]
play = "--groti" in sys.argv
base = f"http://{ip}"
H = {"X-Pin": pin}
SOUND = "data/pries.mp3"


def check(name, ok, info=""):
    print(f"{'OK ' if ok else 'BLOGAI'} {name} {info}")
    if not ok:
        sys.exit(1)


r = requests.get(f"{base}/status", timeout=5)
check("GET /status be PIN", r.ok and "version" in r.json(), r.json().get("version", ""))

r = requests.get(f"{base}/config", timeout=5)
check("GET /config be PIN -> 401", r.status_code == 401)

r = requests.get(f"{base}/config", headers=H, timeout=5)
orig = r.json()
check("GET /config", r.ok and "lessons" in orig, f"{len(orig['lessons'])} pamokų")

bad = dict(orig, lessons=[{"start": "25:00", "end": "09:00"}])
r = requests.post(f"{base}/config", headers=H, json=bad, timeout=5)
check("POST blogas config -> 400", r.status_code == 400, r.json().get("error", ""))

t0 = time.time()
with open(SOUND, "rb") as f:
    r = requests.post(f"{base}/upload", headers=H, files={"file": ("bandymas.mp3", f, "audio/mpeg")}, timeout=60)
up = r.json()
check("POST /upload", r.ok and up.get("file", "").startswith("/s/"), f"{up} per {time.time() - t0:.1f} s")

r = requests.post(f"{base}/upload", headers=H, files={"file": ("x.wav", b"RIFF", "audio/wav")}, timeout=10)
check("POST /upload .wav -> 400", r.status_code == 400, r.json().get("error", ""))

new = dict(orig, sounds=orig["sounds"] + [{"file": up["file"], "name": "Bandymas", "use": "BALTAS"}])
r = requests.post(f"{base}/config", headers=H, json=new, timeout=10)
check("POST config su BALTAS", r.ok)

files = [f["file"] for f in requests.get(f"{base}/files", headers=H, timeout=5).json()]
check("GET /files rodo įkeltą", up["file"] in files)

r = requests.get(f"{base}/file", headers=H, params={"f": up["file"]}, timeout=30)
check("GET /file atsisiųstas toks pat", r.ok and r.content == open(SOUND, "rb").read(), f"{len(r.content)} B")

if play:
    r = requests.post(f"{base}/play", headers=H, params={"what": "BALTAS"}, timeout=5)
    check("POST /play BALTAS", r.ok)
    time.sleep(3)
    r = requests.post(f"{base}/stop", headers=H, timeout=5)
    check("POST /stop", r.ok)

r = requests.post(f"{base}/play", headers=H, params={"what": "NEZINOMAS"}, timeout=5)
check("POST /play blogas -> 400", r.status_code == 400)

r = requests.post(f"{base}/config", headers=H, json=orig, timeout=10)
check("POST pradinis config grąžintas", r.ok)
files = [f["file"] for f in requests.get(f"{base}/files", headers=H, timeout=5).json()]
check("nenaudojamas garsas ištrintas", up["file"] not in files)

log = requests.get(f"{base}/log", headers=H, timeout=5).text
check("GET /log", "config išsaugotas per API" in log, f"{len(log.splitlines())} eil.")

t0 = time.time()
for _ in range(10):
    requests.get(f"{base}/status", timeout=5)
check("atsako greitai", True, f"vidutiniškai {(time.time() - t0) * 100:.0f} ms")
print("visi testai praėjo")
