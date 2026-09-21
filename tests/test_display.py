import copy
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("display",ROOT/"scripts/display.py")
display=importlib.util.module_from_spec(spec); spec.loader.exec_module(display)
class DisplayGates(unittest.TestCase):
    def test_configuration(self): display.config()
    def test_no_pan_without_evidence(self):
        data=display.config(); data["pan_enabled"]=True
        with self.assertRaises(RuntimeError): display.config(data)
    def test_no_mode_switch(self):
        data=display.config(); data["set_display_mode"]=True
        with self.assertRaises(RuntimeError): display.config(data)
    def test_no_vsync_claim(self):
        data=display.config(); data["vsync_enabled"]=True
        with self.assertRaises(RuntimeError): display.config(data)
    def test_no_board_inheritance(self):
        data=display.config(); data["profiles"][1]["board_record"]=data["profiles"][0]["board_record"]
        with self.assertRaises(RuntimeError): display.config(data)
    def test_no_empty_pass(self):
        with self.assertRaises(RuntimeError): display.validate_output("PRESENTER_OK", "native")
    def test_duplicate_cases(self):
        text=''.join(f'PASS {n}\n' for n in sorted(display.UNIT_CASES))
        text+='PASS xrgb8888-padding-offset-canaries\nPRESENTER_OK pointer_bits=64 physical_panel_validated=false\n'
        with self.assertRaises(RuntimeError): display.validate_output(text,"native")
    def test_wrong_architecture(self):
        text=''.join(f'PASS {n}\n' for n in sorted(display.UNIT_CASES))+'PRESENTER_OK pointer_bits=64 physical_panel_validated=false\n'
        with self.assertRaises(RuntimeError): display.validate_output(text,"arm")
    def test_original_font(self):
        data=display.assets.font_bytes(); self.assertEqual(data[:4],b'DCFA'); self.assertEqual(len(data),12336)
    def test_reference_both_resolutions(self):
        for h in (600,800):
            image=display.assets.reference(h); self.assertEqual(len(image),1024*h*3)
            self.assertEqual(image[:3],b'\xff\xff\xff')
            self.assertNotEqual(image,display.assets.reference(h,2))
if __name__=="__main__": unittest.main()
