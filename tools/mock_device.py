"""Bandomasis įrenginys: tas pats HTTP API su pavyzdiniais duomenimis – programos nuotraukoms ir bandymams be ESP32.

    python tools/mock_device.py [prievadas]     (PIN 1234)
"""
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PIN = "1234"
LESSONS = [("08:30", "09:15"), ("09:25", "10:10"), ("10:20", "11:05"), ("11:15", "12:00"),
           ("12:20", "13:05"), ("13:25", "14:10"), ("14:20", "15:05"), ("15:15", "16:00")]
CONFIG = {
    "pre_minutes": 2, "lead_ms": 200, "ring_end": True, "volume": 21, "days": [0, 1, 2, 3, 4],
    "alerts": "play", "alert_place": "Vilniaus",
    "lessons": [{"start": a, "end": b} for a, b in LESSONS],
    "sounds": [
        {"file": "/s/1a2b3c4d.mp3", "name": "Mokyklos skambutis", "use": "bendras"},
        {"file": "/s/5e6f7a8b.mp3", "name": "Varpelis prieš pamoką", "use": "pries"},
        {"file": "/s/9c0d1e2f.mp3", "name": "Civilinė sauga – BALTAS", "use": "BALTAS"},
        {"file": "/s/3a4b5c6d.mp3", "name": "Civilinė sauga – GELTONAS", "use": "GELTONAS"},
        {"file": "/s/7e8f9a0b.mp3", "name": "Civilinė sauga – RAUDONAS", "use": "RAUDONAS"},
        {"file": "/s/c1d2e3f4.mp3", "name": "Kalėdinis skambutis", "use": "off"},
    ],
}
SIZES = {"/s/1a2b3c4d.mp3": 196608, "/s/5e6f7a8b.mp3": 228352, "/s/9c0d1e2f.mp3": 312320,
         "/s/3a4b5c6d.mp3": 298496, "/s/7e8f9a0b.mp3": 305152, "/s/c1d2e3f4.mp3": 401408}
STATUS = {
    "version": "0.5.0", "uptime_s": 86400, "time_ok": True, "time": "2026-10-21 11:42:22", "next_bell": "12:00",
    "wifi": {"ssid": "Sokratus", "rssi": -46, "ip": "192.168.100.40"},
    "rtc": {"ok": True, "lost": False, "temp": 24.5}, "fs": {"used": 1777664, "total": 10354688},
    "playing": False, "update": "naujausia (0.5.0)",
    "alerts": {"mode": "play", "info": "gerai, 11:42", "last": "2026-10-03 09:48 GELTONA – Vilniaus apskritis"},
}
LOG = """2026-10-21 07:58:03 skambutis v0.5.0 – įsijungė: įjungtas maitinimas
2026-10-21 07:58:03 config: /config.json, 8 pamokų, prieš 2 min
2026-10-21 07:58:03 RTC: laikas nustatytas iš laikrodžio
2026-10-21 07:58:05 WiFi: 192.168.100.40
2026-10-21 07:58:05 API: http://192.168.100.40/ (skambutis-esp.local)
2026-10-21 07:58:09 RTC: atnaujintas iš interneto
2026-10-21 08:27:59 1 pam. -2 min: groja /s/5e6f7a8b.mp3
2026-10-21 08:29:59 1 pam. pradžia: groja /s/1a2b3c4d.mp3
2026-10-21 08:58:03 būsena: veikia 1 h, heap 151200 (min 145540), WiFi -46 dBm, RTC 24.5 °C, v0.5.0
2026-10-21 09:14:59 1 pam. pabaiga: groja /s/1a2b3c4d.mp3
2026-10-21 09:22:59 2 pam. -2 min: groja /s/5e6f7a8b.mp3
2026-10-21 09:24:59 2 pam. pradžia: groja /s/1a2b3c4d.mp3
2026-10-21 09:58:03 būsena: veikia 2 h, heap 151196 (min 145540), WiFi -47 dBm, RTC 24.5 °C, v0.5.0
2026-10-21 10:09:59 2 pam. pabaiga: groja /s/1a2b3c4d.mp3
2026-10-21 10:17:59 3 pam. -2 min: groja /s/5e6f7a8b.mp3
2026-10-21 10:19:59 3 pam. pradžia: groja /s/1a2b3c4d.mp3
"""


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _json(self, code, obj):
        self._send(code, json.dumps(obj, ensure_ascii=False))

    def _authed(self):
        if self.headers.get("X-Pin") == PIN:
            return True
        self._json(401, {"error": "neteisingas PIN"})
        return False

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/status":
            return self._json(200, STATUS)
        if not self._authed():
            return
        if path == "/config":
            return self._json(200, CONFIG)
        if path == "/files":
            return self._json(200, [{"file": f, "size": s} for f, s in SIZES.items()])
        if path == "/log":
            return self._send(200, LOG, "text/plain; charset=utf-8")
        self._json(404, {"error": "nėra tokio adreso"})

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        if not self._authed():
            return
        if self.path == "/config":
            CONFIG.clear()
            CONFIG.update(json.loads(body))
        self._json(200, {"ok": True})

    def log_message(self, *a):
        pass


def serve(port=8099) -> ThreadingHTTPServer:
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    serve(port)
    print(f"bandomasis įrenginys: 127.0.0.1:{port}, PIN {PIN}")
    threading.Event().wait()
