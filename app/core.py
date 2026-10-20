"""Ryšys su skambučiu (HTTP API), paieška tinkle ir garso paruošimas. Be GUI – testuojama atskirai."""
import json
import socket
import time
import zipfile
from pathlib import Path

import lameenc
import miniaudio
import numpy as np
import requests

SERVICE = "_skambutis._tcp.local."
USES = {  # sounds[].use -> kaip rodyti žmogui
    "bendras": "Pamokos pradžia ir pabaiga",
    "pries": "Prieš pamoką",
    "BALTAS": "Signalas BALTAS",
    "GELTONAS": "Signalas GELTONAS",
    "RAUDONAS": "Signalas RAUDONAS",
    "off": "Nenaudojamas",
}
SIGNALS = ("BALTAS", "GELTONAS", "RAUDONAS")
RATE = 48000
TARGET_DBFS = -20.0  # garsiųjų dalių lygis – kaip esamo skambučio
PEAK_DBFS = -1.0
MAX_SECONDS = 120


class DeviceError(Exception):
    pass


class Device:
    def __init__(self, ip: str, pin: str):
        self.base = f"http://{ip}"
        self.headers = {"X-Pin": pin}

    def _req(self, method, path, timeout=10, **kw):
        try:
            r = requests.request(method, self.base + path, headers=self.headers, timeout=timeout, **kw)
        except requests.RequestException as e:
            raise DeviceError(f"Nepavyko prisijungti prie {self.base}: {e.__class__.__name__}") from e
        if not r.ok:
            try:
                msg = r.json().get("error", r.text)
            except ValueError:
                msg = r.text
            raise DeviceError(msg or f"HTTP {r.status_code}")
        return r

    def status(self) -> dict:
        return self._req("GET", "/status", timeout=5).json()

    def config(self) -> dict:
        return self._req("GET", "/config").json()

    def save_config(self, cfg: dict):
        self._req("POST", "/config", json=cfg, timeout=20)

    def files(self) -> list:
        return self._req("GET", "/files").json()

    def download(self, file: str) -> bytes:
        return self._req("GET", "/file", params={"f": file}, timeout=60).content

    def upload(self, mp3: bytes, name="garsas.mp3") -> str:
        r = self._req("POST", "/upload", files={"file": (name, mp3, "audio/mpeg")}, timeout=120)
        return r.json()["file"]

    def play(self, what=None, file=None):
        self._req("POST", "/play", params={"what": what} if what else {"file": file})

    def stop(self):
        self._req("POST", "/stop")

    def log(self) -> str:
        return self._req("GET", "/log").text

    def set_pin(self, new_pin: str):
        self._req("POST", "/pin", json={"pin": new_pin})
        self.headers["X-Pin"] = new_pin


def find_devices(timeout=3.0) -> list[tuple[str, str]]:
    """[(vardas, ip)] – įrenginiai, besiskelbiantys _skambutis._tcp vietiniame tinkle."""
    from zeroconf import ServiceBrowser, Zeroconf

    found = {}

    class Listener:
        def add_service(self, zc, type_, name):
            info = zc.get_service_info(type_, name, timeout=2000)
            if info and info.addresses:
                found[name.split(".")[0]] = socket.inet_ntoa(info.addresses[0])

        update_service = add_service

        def remove_service(self, *a):
            pass

    zc = Zeroconf()
    try:
        ServiceBrowser(zc, SERVICE, Listener())
        time.sleep(timeout)
    finally:
        zc.close()
    return sorted(found.items())


def prepare_sound(path: str | Path) -> tuple[bytes, float]:
    """Bet koks garso failas -> (MP3 48 kHz mono, trukmė s): suvienodintas garsumas, nukirpta tyla."""
    try:
        d = miniaudio.decode_file(str(path), output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=RATE)
    except miniaudio.DecodeError as e:
        raise DeviceError(f"Nepavyko perskaityti {Path(path).name}: palaikomi mp3, wav, flac, ogg") from e
    x = np.frombuffer(d.samples, dtype=np.int16).astype(np.float32) / 32768
    loud = np.nonzero(np.abs(x) > 0.003)[0]  # tyla pradžioje vėlina skambutį, gale – tik užima vietą
    if loud.size == 0:
        raise DeviceError(f"{Path(path).name}: tuščias (tyla)")
    x = x[max(0, loud[0] - RATE // 100): loud[-1] + RATE // 10]
    if len(x) > MAX_SECONDS * RATE:
        raise DeviceError(f"{Path(path).name}: per ilgas ({len(x) / RATE:.0f} s, daugiausia {MAX_SECONDS} s)")
    win = RATE // 20  # 50 ms langai; garsumas = 90-as procentilis, kad tylos nesumažintų
    rms = np.sqrt(np.mean(x[: len(x) // win * win].reshape(-1, win) ** 2, axis=1)) if len(x) >= win else np.array([np.sqrt(np.mean(x**2))])
    level = float(np.percentile(rms, 90))
    gain = min(10 ** (TARGET_DBFS / 20) / level, 10 ** (PEAK_DBFS / 20) / float(np.max(np.abs(x))))
    pcm = np.clip(x * gain * 32767, -32768, 32767).astype(np.int16)
    enc = lameenc.Encoder()
    enc.set_bit_rate(128)
    enc.set_in_sample_rate(RATE)
    enc.set_out_sample_rate(RATE)
    enc.set_channels(1)
    enc.set_quality(2)
    return bytes(enc.encode(pcm.tobytes()) + enc.flush()), len(pcm) / RATE


def export_backup(dev: Device, zip_path: str | Path):
    """config.json + visi naudojami garsai -> zip."""
    cfg = dev.config()
    with zipfile.ZipFile(zip_path, "w") as z:
        for i, s in enumerate(cfg["sounds"]):
            arc = f"garsai/{i:02d}.mp3"
            z.writestr(arc, dev.download(s["file"]))
            s["file"] = arc
        z.writestr("config.json", json.dumps(cfg, ensure_ascii=False, indent=2))


def import_backup(dev: Device, zip_path: str | Path):
    """Atvirkščiai: įkelia garsus, perrašo jų kelius ir išsaugo config įrenginyje."""
    with zipfile.ZipFile(zip_path) as z:
        cfg = json.loads(z.read("config.json"))
        for s in cfg["sounds"]:
            s["file"] = dev.upload(z.read(s["file"]), Path(s["file"]).name)
    dev.save_config(cfg)


def settings_path() -> Path:
    import os

    base = Path(os.environ.get("APPDATA") or Path.home() / ".config") / "Skambutis"
    base.mkdir(parents=True, exist_ok=True)
    return base / "nustatymai.json"


def load_settings() -> dict:
    try:
        return json.loads(settings_path().read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_settings(s: dict):
    settings_path().write_text(json.dumps(s, ensure_ascii=False, indent=2), encoding="utf-8")


def mp3_info(data: bytes) -> tuple[int, float]:
    """(Hz, trukmė s) – testams."""
    i = miniaudio.mp3_get_info(data)
    return i.sample_rate, i.duration


if __name__ == "__main__":
    print(find_devices())
