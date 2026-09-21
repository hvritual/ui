"""Synthetic parser fixtures test gates; they are NOT runtime acceptance evidence."""
import copy
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("port", Path(__file__).resolve().parents[1] / "scripts/port.py")
port = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(port)


def arm_elf():
    header = bytearray(52)
    header[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H", header, 18, 40)
    struct.pack_into("<I", header, 36, 0x05000400)
    return header


def valid_output():
    return "".join(f"PASS {name}\n" for name in sorted(port.TESTS)) + "SMOKE_OK tests=10 pointer_bits=32 jsvalue_bytes=8\n"


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.lock = port.read_json(port.LOCK)
        self.target = port.read_json(port.TARGET)

    def test_real_configuration(self):
        port.configs()

    def test_missing_display(self):
        self.target["profiles"].pop()
        with self.assertRaises(RuntimeError): port.validate_target(self.target)

    def test_wrong_resolution(self):
        self.target["profiles"][1]["height"] = 768
        with self.assertRaises(RuntimeError): port.validate_target(self.target)

    def test_hardware_cannot_be_certified(self):
        self.target["rendering"]["hardware_verified"] = True
        with self.assertRaises(RuntimeError): port.validate_target(self.target)

    def test_guessed_ram_rejected(self):
        self.target["device_evidence"]["ram_mib"] = 128
        with self.assertRaises(RuntimeError): port.validate_target(self.target)

    def test_wrong_rust_target(self):
        self.lock["toolchain"]["rust_target"] = "aarch64-unknown-linux-gnu"
        with self.assertRaises(RuntimeError): port.validate_lock(self.lock)

    def test_floating_revision(self):
        self.lock["quickjs"]["revision"] = "main"
        with self.assertRaises(RuntimeError): port.validate_lock(self.lock)

    def test_unknown_repository(self):
        self.lock["quickjs"]["repository"] = "https://example.invalid/replacement.git"
        with self.assertRaises(RuntimeError): port.validate_lock(self.lock)

    def test_source_traversal_rejected(self):
        self.lock["quickjs"]["source_dir"] = "../../outside"
        with self.assertRaises(RuntimeError): port.validate_lock(self.lock)

    def test_missing_hard_float(self):
        self.lock["toolchain"]["c_flags"].remove("-mfloat-abi=hard")
        with self.assertRaises(RuntimeError): port.validate_lock(self.lock)

    def test_missing_tool_fails(self):
        with self.assertRaisesRegex(RuntimeError, "missing required executable"):
            port.executable("ui-tool-that-does-not-exist-a31791")


class ElfTests(unittest.TestCase):
    def test_arm_header(self):
        self.assertEqual(port.elf_header(arm_elf())["machine"], 40)

    def test_not_elf(self):
        with self.assertRaises(RuntimeError): port.elf_header(b"PASS")

    def test_elf64_rejected(self):
        data = arm_elf(); data[4] = 2
        with self.assertRaises(RuntimeError): port.elf_header(data)

    def test_big_endian_rejected(self):
        data = arm_elf(); data[5] = 2
        with self.assertRaises(RuntimeError): port.elf_header(data)

    def test_x86_rejected(self):
        data = arm_elf(); struct.pack_into("<H", data, 18, 62)
        with self.assertRaises(RuntimeError): port.elf_header(data)

    def test_soft_float_rejected(self):
        data = arm_elf(); struct.pack_into("<I", data, 36, 0x05000200)
        with self.assertRaises(RuntimeError): port.elf_header(data)

    def test_conflicting_float_rejected(self):
        data = arm_elf(); struct.pack_into("<I", data, 36, 0x05000600)
        with self.assertRaises(RuntimeError): port.elf_header(data)

    def test_old_eabi_rejected(self):
        data = arm_elf(); struct.pack_into("<I", data, 36, 0x04000400)
        with self.assertRaises(RuntimeError): port.elf_header(data)


class SmokeGateTests(unittest.TestCase):
    def test_complete_output(self):
        port.smoke_result(valid_output(), 0)

    def test_nonzero_exit_rejected(self):
        with self.assertRaises(RuntimeError): port.smoke_result(valid_output(), 1)

    def test_missing_case_rejected(self):
        output = valid_output().replace("PASS interrupt\n", "")
        with self.assertRaises(RuntimeError): port.smoke_result(output, 0)

    def test_duplicate_case_rejected(self):
        with self.assertRaises(RuntimeError): port.smoke_result(valid_output() + "PASS arithmetic\n", 0)

    def test_contradiction_rejected(self):
        with self.assertRaises(RuntimeError): port.smoke_result(valid_output() + "SMOKE_FAILED bad\n", 0)

    def test_wrong_abi_summary_rejected(self):
        with self.assertRaises(RuntimeError): port.smoke_result(valid_output().replace("pointer_bits=32", "pointer_bits=64"), 0)

    def test_missing_summary_rejected(self):
        with self.assertRaises(RuntimeError): port.smoke_result("PASS arithmetic\n", 0)


class SourceIntegrityTests(unittest.TestCase):
    def test_real_git_checkout_rejects_modified_and_wrong_revision(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            def git(*args):
                return subprocess.check_output(["git", *args], cwd=directory, text=True, stderr=subprocess.STDOUT).strip()
            git("init", "--quiet")
            (directory / "VERSION").write_text("test-only fixture\n")
            git("add", "VERSION")
            git("-c", "user.name=Gate test", "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture")
            revision = git("rev-parse", "HEAD")
            with patch.object(port, "OUT", directory / "logs"):
                # Logs live outside the checkout so source cleanliness stays meaningful.
                with tempfile.TemporaryDirectory() as logs, patch.object(port, "OUT", Path(logs)):
                    port.check_checkout(directory, revision)
                    with self.assertRaisesRegex(RuntimeError, "commit mismatch"):
                        port.check_checkout(directory, "0" * 40)
                    (directory / "VERSION").write_text("tampered\n")
                    with self.assertRaisesRegex(RuntimeError, "modified"):
                        port.check_checkout(directory, revision)


if __name__ == "__main__":
    unittest.main()
