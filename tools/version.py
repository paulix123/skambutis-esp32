# PlatformIO pre-script: FW_VERSION = git describe (v0.3.0, v0.3.0-2-gabc-dirty arba hash) – OTA lygina su leidimais
import subprocess

Import("env")  # noqa: F821 – PlatformIO SCons

try:
    ver = subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"], cwd=env["PROJECT_DIR"]).decode().strip()  # noqa: F821
except Exception:
    ver = "dev"
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(ver))])  # noqa: F821
print("FW_VERSION =", ver)
