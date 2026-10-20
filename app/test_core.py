"""uv run --with zeroconf --with miniaudio --with lameenc --with numpy --with requests app/test_core.py [IP PIN]

Be argumentų – tik garso paruošimas; su IP ir PIN – dar paieška tinkle, įkėlimas ir atsarginė kopija
(config pabaigoje toks pat kaip pradžioje)."""
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import core  # noqa: E402

ROOT = Path(__file__).parent.parent


def dbfs(x):
    return 20 * np.log10(max(x, 1e-9))


def check_sound(path):
    mp3, dur = core.prepare_sound(path)
    rate, dur2 = core.mp3_info(mp3)
    import miniaudio
    d = miniaudio.decode(mp3, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=48000)
    x = np.frombuffer(d.samples, dtype=np.int16).astype(np.float32) / 32768
    win = 2400
    rms = np.sqrt(np.mean(x[: len(x) // win * win].reshape(-1, win) ** 2, axis=1))
    level, peak = dbfs(np.percentile(rms, 90)), dbfs(np.max(np.abs(x)))
    print(f"  {Path(path).name}: {dur:.1f} s, {rate} Hz, garsumas {level:.1f} dBFS, pikas {peak:.1f} dBFS, {len(mp3) // 1024} KB")
    assert rate == 48000
    assert abs(dur2 - dur) < 0.2
    assert peak <= -0.5, "pikas per aukštas"
    return mp3, dur, level


def write_wav(path, sig, rate=48000, ch=1):
    with wave.open(str(path), "wb") as f:
        f.setnchannels(ch); f.setsampwidth(2); f.setframerate(rate)
        f.writeframes((np.clip(sig, -1, 1) * 32767).astype(np.int16).tobytes())


print("garso paruošimas:")
for real in (ROOT / "data/pries.mp3", ROOT.parent / "skambutis/garsai/skambutis double.wav"):
    if real.exists():  # tikri garsai – tik kūrėjo kompiuteryje, ne CI
        check_sound(real)
with tempfile.TemporaryDirectory() as tmp:
    t = np.arange(44100 * 2) / 44100
    loud = Path(tmp) / "garsus_stereo_44k.wav"  # garsus, stereo, kitas dažnis – pikas ribojamas
    write_wav(loud, np.repeat(0.99 * np.sin(2 * np.pi * 880 * t), 2), rate=44100, ch=2)
    check_sound(loud)
    mp3_in = Path(tmp) / "is_mp3.mp3"  # mp3 įvestis
    mp3_in.write_bytes(core.prepare_sound(loud)[0])
    check_sound(mp3_in)
    # tylus 440 Hz su 1 s tyla pradžioje: turi būti pagarsintas iki ~-20 dBFS ir nukirpta tyla
    w = Path(tmp) / "tylus.wav"
    t = np.arange(48000 * 2) / 48000
    write_wav(w, np.concatenate([np.zeros(48000), 0.01 * np.sin(2 * np.pi * 440 * t)]))
    _, dur, level = check_sound(w)
    assert dur < 2.2, "tyla pradžioje nenukirpta"
    assert abs(level - core.TARGET_DBFS) < 1.5, "garsumas nesuvienodintas"
    bad = Path(tmp) / "blogas.mp3"
    bad.write_bytes(b"ne garsas")
    try:
        core.prepare_sound(bad)
        raise AssertionError("blogas failas priimtas")
    except core.DeviceError as e:
        print("  blogas failas atmestas:", e)

if len(sys.argv) < 3:
    print("OK (be įrenginio)")
    sys.exit()

ip, pin = sys.argv[1], sys.argv[2]
print("paieška tinkle:", found := core.find_devices())
assert any(a == ip for _, a in found), "įrenginys nerastas per mDNS"
dev = core.Device(ip, pin)
try:
    core.Device(ip, "0000").config()
    raise AssertionError("blogas PIN priimtas")
except core.DeviceError as e:
    print("blogas PIN atmestas:", e)
st = dev.status()
print("būsena:", st["version"], st.get("time"), "sekantis", st.get("next_bell"))
orig = dev.config()
w2 = Path(tempfile.mkdtemp()) / "bandymas.wav"
write_wav(w2, 0.3 * np.sin(np.arange(48000) / 8))
mp3 = core.prepare_sound(w2)[0]
tmpfile = dev.upload(mp3, "bandymas.mp3")
print("įkelta:", tmpfile)
with tempfile.TemporaryDirectory() as tmp:
    z = Path(tmp) / "kopija.zip"
    core.export_backup(dev, z)
    print("kopija:", z.stat().st_size // 1024, "KB")
    core.import_backup(dev, z)
after = dev.config()
assert after["lessons"] == orig["lessons"] and len(after["sounds"]) == len(orig["sounds"])
assert [s["use"] for s in after["sounds"]] == [s["use"] for s in orig["sounds"]]
files = [f["file"] for f in dev.files()]
assert tmpfile not in files, "nenaudojamas įkeltas failas neištrintas"
print("atkurta iš kopijos, config toks pat; failai:", files)
print("OK")
