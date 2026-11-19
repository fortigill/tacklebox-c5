# PlatformIO post-build check: does firmware.bin fit the OTA app slot?
#
# PlatformIO's "Flash: x%" figure is based on the ELF sections, but OTA writes
# the .bin image, which is larger (headers, padding). The device rejects an
# image bigger than its slot, so check the real file and fail the build early.
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

SLOT_BYTES = 0x1E0000      # app0/app1 size in min_spiffs.csv
WARN_MARGIN = 64 * 1024


def check_size(source, target, env):
    path = str(target[0])
    size = os.path.getsize(path)
    free = SLOT_BYTES - size
    print(f"OTA slot: firmware.bin is {size} of {SLOT_BYTES} bytes "
          f"({size * 100.0 / SLOT_BYTES:.1f}%, {free // 1024} KB free)")
    if size > SLOT_BYTES:
        print("ERROR: firmware.bin is larger than the OTA app slot; the device would reject it.")
        env.Exit(1)
    elif free < WARN_MARGIN:
        print("WARNING: less than 64 KB left in the OTA app slot.")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_size)  # noqa: F821
