# PlatformIO post-build check: does firmware.bin fit the OTA app slot?
#
# PlatformIO's "Flash: x%" figure is based on the ELF sections, but OTA writes
# the .bin image, which is larger (headers, padding). The device rejects an
# image bigger than its slot, so check the real file and fail the build early.
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

WARN_MARGIN = 64 * 1024


def slot_bytes():
    """Size of the app0 slot in this environment's partition table."""
    csv = env.subst("$PARTITIONS_TABLE_CSV")  # noqa: F821
    try:
        with open(csv, encoding="utf-8") as f:
            for line in f:
                cols = [c.strip() for c in line.split("#")[0].split(",")]
                if len(cols) >= 5 and cols[0] == "app0":
                    return int(cols[4], 0)
    except OSError:
        pass
    return 0x1E0000  # min_spiffs.csv (classic CYD)


SLOT_BYTES = slot_bytes()


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
