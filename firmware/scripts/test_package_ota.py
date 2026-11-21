import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from package_ota import application_version, create_manifest, validate_application, write_manifest_exclusive


COMMIT = "0123456789abcdef0123456789abcdef01234567"


def make_image(version="tag-check"):
    image = bytearray(336)
    image[0] = 0xE9
    image[1] = 1
    struct.pack_into("<H", image, 12, 9)
    image[23] = 1
    struct.pack_into("<I", image, 28, 256)
    struct.pack_into("<I", image, 32, 0xABCD5432)
    version_bytes = version.encode("ascii")
    image[48:48 + len(version_bytes)] = version_bytes
    image[48 + len(version_bytes)] = 0
    image[80:84] = b"BYAI"
    image[84] = 0

    checksum = 0xEF
    for value in image[32:288]:
        checksum ^= value
    image[303] = checksum
    image[304:336] = hashlib.sha256(image[:304]).digest()
    return bytes(image)


class PackageOtaTests(unittest.TestCase):
    def test_branch_and_tag_version_rules(self):
        self.assertEqual(application_version("dev", "dev", COMMIT), "dev-0123456789ab")
        self.assertEqual(application_version("main", "main", COMMIT), "main-0123456789ab")
        self.assertEqual(application_version("tag", "v1.2.3", COMMIT), "v1.2.3")
        self.assertEqual(application_version("tag", "a" * 32, COMMIT), "tag-0123456789ab")
        self.assertEqual(application_version("tag", "发布-1", COMMIT), "tag-0123456789ab")

    def test_rejects_invalid_source_ref_and_commit(self):
        for source, ref, commit in (
            ("other", "other", COMMIT),
            ("dev", "main", COMMIT),
            ("main", "main", "0123"),
            ("tag", "", COMMIT),
        ):
            with self.subTest(source=source, ref=ref, commit=commit):
                with self.assertRaises(ValueError):
                    application_version(source, ref, commit)

    def test_manifest_binds_image_bytes_and_expected_identity(self):
        image = make_image()
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "input.bin"
            image_path.write_bytes(image)
            manifest = create_manifest(image_path, "tag", "tag-check", COMMIT, "tag-check")

        self.assertEqual(manifest["schema_version"], 1)
        self.assertEqual(manifest["project"], "BYAI")
        self.assertEqual(manifest["chip"], "esp32s3")
        self.assertEqual(manifest["commit"], COMMIT)
        self.assertEqual(manifest["application"], {
            "name": "BYAI_firmware_0123456789ab.bin",
            "size": len(image),
            "sha256": hashlib.sha256(image).hexdigest(),
        })

    def test_rejects_wrong_image_version_and_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "input.bin"
            image_path.write_bytes(make_image("other"))
            with self.assertRaisesRegex(ValueError, "does not match"):
                create_manifest(image_path, "tag", "tag-check", COMMIT, "tag-check")

            corrupted = bytearray(make_image())
            corrupted[200] ^= 1
            image_path.write_bytes(corrupted)
            with self.assertRaisesRegex(ValueError, "checksum mismatch|SHA-256 mismatch"):
                validate_application(bytes(corrupted))

    def test_rejects_invalid_segment_metadata(self):
        image = bytearray(make_image())
        image[1] = 0
        with self.assertRaisesRegex(ValueError, "segment count"):
            validate_application(bytes(image))

        image = bytearray(make_image())
        struct.pack_into("<I", image, 28, 255)
        with self.assertRaisesRegex(ValueError, "full descriptor"):
            validate_application(bytes(image))

    def test_rejects_wrong_chip_project_length_and_tail_digest(self):
        image = bytearray(make_image())
        image[12] = 0
        with self.assertRaisesRegex(ValueError, "not ESP32-S3"):
            validate_application(bytes(image))

        image = bytearray(make_image())
        image[80] = ord('X')
        with self.assertRaisesRegex(ValueError, "project must be BYAI"):
            validate_application(bytes(image))

        with self.assertRaisesRegex(ValueError, "size outside"):
            validate_application(b'')
        with self.assertRaisesRegex(ValueError, "size outside"):
            validate_application(b'\0' * (0x300000 + 1))

        image = bytearray(make_image())
        image[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            validate_application(bytes(image))

    def test_output_creation_is_exclusive_and_cli_does_not_leave_failure_file(self):
        manifest = {"schema_version": 1}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "manifest.json"
            write_manifest_exclusive(output, manifest)
            original = output.read_bytes()
            with self.assertRaises(FileExistsError):
                write_manifest_exclusive(output, {"schema_version": 2})
            self.assertEqual(output.read_bytes(), original)

            image_path = root / "image.bin"
            image_path.write_bytes(make_image("wrong"))
            failed_output = root / "failed.json"
            result = subprocess.run([
                sys.executable, str(Path(__file__).with_name("package_ota.py")),
                "--image", str(image_path), "--output", str(failed_output),
                "--source", "tag", "--ref", "tag-check", "--commit", COMMIT,
                "--version", "tag-check",
            ], check=False, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(failed_output.exists())
            self.assertIn("does not match", result.stderr)


if __name__ == "__main__":
    unittest.main()
