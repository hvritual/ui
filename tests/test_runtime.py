"""Parser/contract negative tests. They do not replace real QuickJS/core execution."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("runtime", ROOT / "scripts/runtime.py")
runtime = importlib.util.module_from_spec(spec); spec.loader.exec_module(runtime)

class RuntimeConfigTests(unittest.TestCase):
    def setUp(self):
        self.profiles = runtime.read_json(runtime.PROFILES)
        self.board = runtime.read_json(ROOT / "targets/boards/myimx6ek140-1024x600.json")
    def test_real_configs(self): runtime.config()
    def reject_profile(self, mutation):
        mutation(self.profiles)
        with self.assertRaises(RuntimeError): runtime.validate_profiles(self.profiles)
    def test_missing_viewport(self): self.reject_profile(lambda p: p["profiles"].pop())
    def test_wrong_viewport(self): self.reject_profile(lambda p: p["profiles"][1].update(height=768))
    def test_unverified_800_board(self): self.reject_profile(lambda p: p["profiles"][1].update(board_record="targets/boards/myimx6ek140-1024x600.json"))
    def test_unregistered_platform_claim(self): self.reject_profile(lambda p: p.update(upstream_registered=True))
    def test_clock_changed(self): self.reject_profile(lambda p: p.update(simulation_hz=30))
    def test_wrong_pixel_format(self): self.reject_profile(lambda p: p.update(pixel_format="RGB565"))
    def test_ime_claim(self): self.reject_profile(lambda p: p["text_capabilities"].update(ime_composition="supported"))
    def test_guessed_libc(self):
        self.board["unknown"]["libc_version"] = "2.28"
        with self.assertRaises(RuntimeError): runtime.validate_board(self.board)
    def test_wrong_observed_ram(self):
        self.board["observed"]["mem_total_kib"] = 256 * 1024
        with self.assertRaises(RuntimeError): runtime.validate_board(self.board)
    def test_wrong_observed_bpp(self):
        self.board["observed"]["fbset"]["geometry"][-1] = 16
        with self.assertRaises(RuntimeError): runtime.validate_board(self.board)
    def test_changed_evidence(self):
        self.board["provenance"]["sha256"] = "0" * 64
        with self.assertRaises(RuntimeError): runtime.validate_board(self.board)
    def test_false_board_execution(self):
        self.board["provenance"]["runtime_executed_on_board"] = True
        with self.assertRaises(RuntimeError): runtime.validate_board(self.board)

class RuntimeResultTests(unittest.TestCase):
    def output(self):
        return "".join("PASS " + s + "\n" for s in sorted(runtime.TESTS)) + "RUNTIME_OK pointer_bits=32 hardware_tested=false\n"
    def test_complete(self): runtime.validate_test(self.output(), "arm")
    def test_missing_case(self):
        with self.assertRaises(RuntimeError): runtime.validate_test(self.output().replace("PASS pause-resume\n", ""), "arm")
    def test_duplicate_case(self):
        with self.assertRaises(RuntimeError): runtime.validate_test(self.output() + "PASS pause-resume\n", "arm")
    def test_wrong_architecture(self):
        with self.assertRaises(RuntimeError): runtime.validate_test(self.output(), "native")
    def test_contradictory_failure(self):
        with self.assertRaises(RuntimeError): runtime.validate_test(self.output() + "TEST_FAILED synthetic\n", "arm")
    def test_missing_summary(self):
        with self.assertRaises(RuntimeError): runtime.validate_test(self.output().replace("RUNTIME_OK", "FAKE_OK"), "arm")


class TextContractTests(unittest.TestCase):
    def setUp(self): self.contract = runtime.read_json(ROOT / "contracts/text-input.json")
    def test_no_false_ime_admission(self):
        self.contract["admission"]["offline_ime"] = True
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_encoding_boundary(self):
        self.contract["js_range_unit"] = "bytes"
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_sensitive_logging(self):
        self.contract["sensitive_field_policy"]["log_text"] = True
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_bounded_queue(self):
        self.contract["queue_contract"]["max_events"] = 0
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)

    def test_native_core_contract(self): runtime.validate_text_contract(self.contract)
    def test_legacy_contract(self):
        self.contract["status"] = "contract-only-not-implemented"
        self.contract["admission"] = {"editable_widget": False, "composition_bridge": False, "offline_ime": False}
        self.contract["identity_fields"] = ["field_id", "session_id", "focus_generation", "revision"]
        runtime.validate_text_contract(self.contract)
    def test_no_false_keyboard_admission(self):
        self.contract["admission"]["keyboard_ui"] = True
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_no_false_owner_bridge(self):
        self.contract["admission"]["F6_owner_bridge"] = True
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_engine_generation_required(self):
        self.contract["identity_fields"].remove("engine_generation")
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_no_false_queue_admission(self):
        self.contract["queue_contract"]["status"] = "implemented"
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
    def test_empty_privacy_policy_rejected(self):
        self.contract["sensitive_field_policy"] = {}
        with self.assertRaises(RuntimeError): runtime.validate_text_contract(self.contract)
