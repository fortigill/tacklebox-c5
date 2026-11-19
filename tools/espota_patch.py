# PlatformIO post-script for the cyd-ota environment.
#
# The device asks "Accept update / Do not allow" before taking an OTA update.
# Stock espota.py only waits a hard-coded 10 s for the device to connect back
# after the invitation (the --timeout option covers the invitation only), which
# leaves too little time to answer. This copies the framework's espota.py to
# .pio/espota_cyd.py with that wait raised to ACCEPT_TIMEOUT_S and points the
# platform's espota uploader ($UPLOADER) at the copy. It runs as a post script
# so it comes after the platform has set up its own uploader. The framework
# file is left untouched.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

ACCEPT_TIMEOUT_S = 45  # device-side question times out at 30 s

platform = env.PioPlatform()  # noqa: F821
candidates = []
for pkg in ("framework-arduinoespressif32", "tool-espotapy"):
    try:
        d = platform.get_package_dir(pkg)  # raises KeyError if the platform doesn't list it
    except Exception:
        d = None
    if d:
        candidates += [os.path.join(d, "tools", "espota.py"), os.path.join(d, "espota.py")]
src_path = next((p for p in candidates if os.path.isfile(p)), None)
if not src_path:
    raise SystemExit("espota_patch: can't find espota.py in " + ", ".join(candidates))

with open(src_path, encoding="utf-8") as f:
    src = f.read()

accept = src.find("sock.accept()")
needle = "sock.settimeout(10)"
pos = src.rfind(needle, 0, accept) if accept >= 0 else -1
if pos < 0:
    raise SystemExit("espota_patch: espota.py layout changed, couldn't find the accept timeout in " + src_path)

patched = (src[:pos] + "sock.settimeout(%d)  # CYD: time to approve on the device" % ACCEPT_TIMEOUT_S
           + src[pos + len(needle):])

out_dir = os.path.join(env.subst("$PROJECT_DIR"), ".pio")  # noqa: F821
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "espota_cyd.py")
old = None
if os.path.isfile(out_path):
    with open(out_path, encoding="utf-8") as f:
        old = f.read()
if old != patched:
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(patched)
    print("espota_patch: wrote %s (device connect-back wait %d s)" % (out_path, ACCEPT_TIMEOUT_S))

# Use the patched copy for `pio run -e cyd-ota -t upload`
if env.subst("$UPLOAD_PROTOCOL") == "espota":  # noqa: F821
    env.Replace(UPLOADER=out_path)  # noqa: F821
    print("espota_patch: OTA uploads use %s" % out_path)
