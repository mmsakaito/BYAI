#!/usr/bin/env python3
"""Migrate the known factory layout without erasing NVS or PHY.

Run with the activated ESP-IDF Python after authorizing serial writes. Backups
contain device secrets. The device remains in ROM download mode until EN reset.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


OLD_END = 0x1D0000
NEW = [("nvs", 1, 2, 0x9000, 0x6000),
       ("phy_init", 1, 1, 0xF000, 0x1000),
       ("otadata", 1, 0, 0x10000, 0x2000),
       ("ota_0", 0, 16, 0x20000, 0x300000),
       ("ota_1", 0, 17, 0x320000, 0x300000)]
OLD = NEW[:2] + [("factory", 0, 0, 0x10000, 0x1C0000)]
BACKUPS = ("factory-region.bin", "partition-table.bin", "nvs.bin", "phy.bin",
           "nvs.sha256", "migration.json")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def exclusive(path, data):
    """Persist a private backup without ever replacing an existing file."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def load_parser():
    sdk = os.environ.get("IDF_PATH") or os.environ.get("ESP_IDF_PATH")
    require(sdk, "Activate the ESP-IDF environment first")
    path = Path(sdk) / "components/partition_table/gen_esp32part.py"
    spec = importlib.util.spec_from_file_location("gen_esp32part", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.offset_part_table = 0x8000
    return module


def table_check(parser, data, expected, label):
    table = parser.PartitionTable.from_binary(data)
    table.verify()
    actual = [(p.name, p.type, p.subtype, p.offset, p.size) for p in table]
    require(actual == expected, f"{label} layout differs; no migration permitted. "
            f"Expected {expected!r}; actual {actual!r}")
    require(all(not p.get_flags_list() for p in table), f"{label} has partition flags")
    # The SDK warns rather than rejects unknown flag bits. Reject every flag here.
    for index in range(len(table)):
        require(struct.unpack_from("<I", data, index * 32 + 28)[0] == 0,
                f"{label} contains unsupported flags")


def image_check(data):
    require(288 <= len(data) <= 0x300000, "Application size outside OTA slot bounds")
    require(data[0] == 0xE9 and data[23] == 1, "Application must have appended SHA-256")
    require(struct.unpack_from("<H", data, 12)[0] == 9, "Application is not ESP32-S3")
    require(struct.unpack_from("<I", data, 32)[0] == 0xABCD5432,
            "Application descriptor magic is invalid")
    project = data[80:112]
    version = data[48:80]
    require(b"\0" in project and project.split(b"\0", 1)[0] == b"BYAI",
            "Application project must be BYAI")
    require(b"\0" in version and version[0] != 0, "Invalid application version")
    position = 24
    checksum = 0xEF
    for _ in range(data[1]):
        require(position + 8 <= len(data), "Truncated segment header")
        length = struct.unpack_from("<I", data, position + 4)[0]
        position += 8
        require(position + length <= len(data), "Truncated application segment")
        for value in data[position:position + length]:
            checksum ^= value
        position += length
    checksum_end = (position + 16) & ~15
    require(checksum_end + 32 == len(data), "Unexpected application trailer or signature")
    require(data[checksum_end - 1] == checksum, "Application checksum mismatch")
    require(hashlib.sha256(data[:checksum_end]).digest() == data[checksum_end:],
            "Application SHA-256 mismatch")


def prepare(build, parser):
    cfg = json.loads((build / "config/sdkconfig.json").read_text())
    # Use the generated build configuration, never an unrelated source sdkconfig.
    require(cfg.get("IDF_TARGET") == "esp32s3", "Build target must be esp32s3")
    require(cfg.get("PARTITION_TABLE_OFFSET") == 0x8000, "Table offset must be 0x8000")
    require(cfg.get("ESPTOOLPY_FLASHSIZE") == "8MB", "Build must configure 8MB Flash")
    require(cfg.get("BOOTLOADER_APP_ROLLBACK_ENABLE") is True, "Rollback must be enabled")
    require(not any(cfg.get(key) for key in ("SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED",
                "SECURE_SIGNED_APPS_NO_SECURE_BOOT", "BOOTLOADER_APP_ANTI_ROLLBACK")),
            "Secure boot/encryption/signing/anti-rollback builds are unsupported")
    args = json.loads((build / "flasher_args.json").read_text())
    require(args["extra_esptool_args"]["chip"] == "esp32s3", "Flash target mismatch")
    settings = args["flash_settings"]
    require(set(settings) == {"flash_mode", "flash_freq", "flash_size"},
            "Unsupported flash settings")
    require(settings["flash_size"] == "8MB", "Flash arguments must configure 8MB")
    require(settings["flash_mode"] in ("dio", "qio", "dout", "qout"), "Unknown flash mode")
    require(settings["flash_freq"] == cfg.get("ESPTOOLPY_FLASHFREQ"), "Flash frequency mismatch")
    files = {}
    for offset, name in args["flash_files"].items():
        address = int(offset, 0)
        require(address not in files, "Duplicate flash offset")
        path = (build / name).resolve()
        require(path.is_relative_to(build), "Flash file escapes build directory")
        data = path.read_bytes()
        require(data, f"Empty flash file: {name}")
        start, end = address & ~0xFFF, (address + len(data) + 0xFFF) & ~0xFFF
        require(not (start < 0x10000 and end > 0x9000),
                f"Flash file {name} would erase NVS/PHY")
        files[address] = (data, start, end)
    require(set(files) == {0, 0x8000, 0x10000, 0x20000}, "Unexpected flash file set")
    ordered = sorted(files)
    for previous, current in zip(ordered, ordered[1:]):
        require(files[previous][2] <= files[current][1], "Flash erase sectors overlap")
    require(files[0][2] <= 0x8000, "Bootloader overlaps partition table")
    require(files[0x8000][2] <= 0x9000, "Partition table exceeds its sector")
    table_check(parser, files[0x8000][0], NEW, "New")
    require(files[0x10000][0] == b"\xff" * 8192, "otadata must be SDK blank 8192-byte image")
    image_check(files[0x20000][0])
    return cfg, settings, files


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--port", required=True)
    cli.add_argument("--backup-dir", required=True, type=Path)
    cli.add_argument("--build-dir", required=True, type=Path)
    options = cli.parse_args()
    build = options.build_dir.resolve(strict=True)
    backup = options.backup_dir.resolve()
    require(not backup.is_relative_to(Path(__file__).resolve().parents[2]),
            "Backup directory must be outside the repository")
    parser = load_parser()
    cfg, settings, files = prepare(build, parser)
    backup.mkdir(mode=0o700, parents=True, exist_ok=True)
    require(backup.is_dir(), "Backup destination is not a directory")
    require(not any((backup / name).exists() or (backup / name).is_symlink()
                    for name in BACKUPS), "Backup files already exist; choose another directory")

    def command(*arguments):
        result = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3",
                                 "--port", options.port, "--before", "default-reset",
                                 "--after", "no-reset", *arguments],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, check=False)
        require(result.returncode == 0,
                "esptool failed; device must remain in download mode:\n" + result.stdout)
        return result.stdout

    security = command("get-security-info")
    require("Secure Boot: Disabled" in security and "Flash Encryption: Disabled" in security,
            "Device security state is not explicitly disabled; refusing migration")
    identity = command("flash-id")
    match = re.search(r"Detected flash size:\s*(\d+)MB", identity)
    require(match and int(match.group(1)) >= 8, "Physical Flash must be at least 8MB")
    if cfg.get("SPIRAM"):
        psram = re.search(r"Embedded PSRAM (\d+)MB", identity)
        require(psram, "Cannot verify installed PSRAM from chip features; no write performed")
        size = int(psram.group(1))
        require((cfg.get("SPIRAM_MODE_OCT") and size in (8, 16)) or
                (cfg.get("SPIRAM_MODE_QUAD") and size in (2, 4)),
                "Embedded PSRAM capacity does not match configured bus mode")
        require(cfg.get("SPIRAM_TYPE_AUTO"), "Only automatic PSRAM detection is supported")

    with tempfile.TemporaryDirectory(prefix="byai-migration-") as work:
        work = Path(work)

        def read_region(address, length, name):
            path = work / name
            command("read-flash", hex(address), hex(length), str(path))
            data = path.read_bytes()
            require(len(data) == length, "Incomplete Flash read")
            return data

        original = read_region(0, OLD_END, "original.bin")
        table_check(parser, original[0x8000:0x9000], OLD, "Device factory")
        nvs, phy = original[0x9000:0xF000], original[0xF000:0x10000]
        digest = hashlib.sha256(nvs).hexdigest()
        exclusive(backup / "factory-region.bin", original)
        exclusive(backup / "partition-table.bin", original[0x8000:0x9000])
        exclusive(backup / "nvs.bin", nvs)
        exclusive(backup / "phy.bin", phy)
        exclusive(backup / "nvs.sha256", (digest + "  nvs.bin\n").encode())
        metadata = {"chip": "esp32s3", "region_size": OLD_END,
                    "region_sha256": hashlib.sha256(original).hexdigest(),
                    "nvs_sha256": digest, "flash_settings": settings,
                    "images": {hex(address): hashlib.sha256(item[0]).hexdigest()
                               for address, item in files.items()}}
        exclusive(backup / "migration.json", (json.dumps(metadata, indent=2) + "\n").encode())
        # Re-open persisted backups before allowing any flash write.
        require((backup / "factory-region.bin").read_bytes() == original and
                (backup / "nvs.bin").read_bytes() == nvs and
                (backup / "phy.bin").read_bytes() == phy,
                "Backup readback mismatch; no write performed")
        # Persist new directory entries as well as the files before touching Flash.
        # Sync from the backup directory up to the filesystem root, including any
        # parents that mkdir(parents=True) created during this invocation.
        for directory in (backup, *backup.parents):
            directory_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        write_args = ["write-flash"]
        for key in ("flash_mode", "flash_freq", "flash_size"):
            write_args.extend(["--" + key.replace("_", "-"), settings[key]])
        # Freeze validated bytes: never write build files that could change mid-run.
        for address, (data, _, _) in sorted(files.items()):
            path = work / f"image-{address:x}.bin"
            path.write_bytes(data)
            write_args.extend([hex(address), str(path)])
        command(*write_args)
        require(read_region(0x9000, 0x6000, "nvs-after.bin") == nvs,
                "NVS READBACK MISMATCH: keep ROM download mode; preserve backups")
        require(read_region(0xF000, 0x1000, "phy-after.bin") == phy,
                "PHY READBACK MISMATCH: keep ROM download mode; preserve backups")
    print("Migration complete: NVS and PHY match backups byte for byte. "
          "Device remains in ROM download mode. Press EN to boot, then use idf.py monitor.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, KeyError, TypeError, ImportError) as error:
        print(f"Migration stopped: {error}", file=sys.stderr)
        sys.exit(1)
