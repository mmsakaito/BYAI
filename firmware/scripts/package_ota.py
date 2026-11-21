#!/usr/bin/env python3
"""Validate a BYAI application image and write its OTA release manifest."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

from migrate_ota import image_check, require


MAX_IMAGE_SIZE = 0x300000
MAX_SEGMENTS = 16
MANIFEST_NAME = "BYAI_OTA.json"


def application_version(source: str, ref: str, commit: str) -> str:
    """Return the exact firmware version encoded for a branch or tag build."""
    require(source in ("dev", "main", "tag"), "source must be dev, main or tag")
    require(re.fullmatch(r"[0-9a-f]{40}", commit) is not None,
            "commit must be a full lowercase SHA-1")
    require(bool(ref) and "\0" not in ref, "ref must be non-empty and contain no NUL")
    short_sha = commit[:12]
    if source in ("dev", "main"):
        require(ref == source, f"{source} builds must use ref={source}")
        return f"{source}-{short_sha}"
    if re.fullmatch(r"[A-Za-z0-9._+-]{1,31}", ref):
        return ref
    return f"tag-{short_sha}"


def validate_application(data: bytes) -> str:
    """Run migration's full image check and return its NUL-terminated version."""
    require(288 <= len(data) <= MAX_IMAGE_SIZE,
            "Application size outside OTA slot bounds")
    require(1 <= data[1] <= MAX_SEGMENTS,
            "Application segment count outside supported bounds")
    first_segment_length = int.from_bytes(data[28:32], "little")
    require(first_segment_length >= 256,
            "First application segment does not contain the full descriptor")
    image_check(data)

    raw_version = data[48:80]
    terminator = raw_version.find(b"\0")
    require(terminator > 0, "Invalid application version")
    try:
        return raw_version[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("Application version must be ASCII") from error


def create_manifest(image_path: Path, source: str, ref: str, commit: str,
                    version: str) -> dict:
    expected_version = application_version(source, ref, commit)
    require(version == expected_version,
            f"version must equal computed application version {expected_version!r}")
    data = image_path.read_bytes()
    image_version = validate_application(data)
    require(image_version == expected_version,
            f"Image version {image_version!r} does not match {expected_version!r}")
    short_sha = commit[:12]
    return {
        "schema_version": 1,
        "project": "BYAI",
        "chip": "esp32s3",
        "source": source,
        "ref": ref,
        "commit": commit,
        "version": expected_version,
        "application": {
            "name": f"BYAI_firmware_{short_sha}.bin",
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        },
    }


def write_manifest_exclusive(output: Path, manifest: dict) -> None:
    """Atomically create a manifest without replacing an existing output."""
    payload = (json.dumps(manifest, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=output.parent,
                                         prefix=f".{output.name}.", delete=False) as stream:
            temporary_path = Path(stream.name)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary_path, output)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source", required=True, choices=("dev", "main", "tag"))
    parser.add_argument("--ref", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    manifest = create_manifest(args.image, args.source, args.ref,
                               args.commit, args.version)
    write_manifest_exclusive(args.output, manifest)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        print(f"OTA packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
