#!/usr/bin/env python3
"""External HIL evidence consistency gate; this program never creates approval.

Each <REPORT>/<profile>/ has the untouched report.json, display.json,
timeline.csv and actual LCD video/photo. review.json requires:
 {"reviewer":"name", "device_serial":"id", "approved":true,
  "package_binary_sha256":"...", "report_sha256":"...",
  "checks":{"lcd_touch":true,"modal_no_clickthrough":true,
    "virtualization":true,"media_update":true,"fault_recovery":true},
  "evidence":{"actual-lcd.mp4":"sha256", "display.json":"sha256",
    "timeline.csv":"sha256"}}
This is human attestation, NOT a cryptographic proof of physical execution.
Never change visual_validated in the original automatic runtime report.
"""
from __future__ import annotations
import argparse,csv,hashlib,json,sys,unittest
from pathlib import Path
PROFILES=['imx6ul-1024x600','imx6ul-1024x800']
CHECKS=['lcd_touch','modal_no_clickthrough','virtualization','media_update','fault_recovery']
def require(v,message):
    if not v:raise ValueError(message)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def runtime_gate(r,profile,commit):
    require(r.get('schema')==1 and r.get('profile')==profile and r.get('commit')==commit,'wrong source/profile')
    require(r.get('ok') is True and r.get('error') is None,'runtime failed')
    require(r.get('physical_io') is True and not r.get('synthetic',False),'physical execution not evidenced')
    require(r.get('visual_validated') is False and r.get('business_commands') is False,'automatic/hardware scope changed')
    for key in ('unblank_errno','display_cleanup_errno','input_cleanup_errno','core_live_bytes_after_close'):
        require(r.get(key)==0,'cleanup error: '+key)
    require(r.get('page_mask')==15 and r.get('modal_seen')==1 and r.get('completed',0)>=1,'incomplete page flow')
    require(r.get('input_frames',0)>10 and r.get('presents',0)>1,'missing physical input/display work')
    require(0<r.get('pool',0)<=6 and r.get('peak_pool',99)<=6 and 0<r.get('nodes',0)<=64 and r.get('recycled',0)>=2,'virtualization evidence missing')
    require(r.get('media_applied',0)>=1,'dynamic resource evidence missing')
    require(r.get('disconnects',0)>=1 and r.get('reconnects',0)>=1 and r.get('syn_dropped',0)>=1,'independent input fault evidence missing')
    require(r.get('wall_seconds',0)>0 and r.get('peak_rss_kib',0)>0 and r.get('cpu_percent_one_core',-1)>=0,'usage data missing')
def verify(base,manifest):
    m=json.loads(manifest.read_text());require(m.get('profiles')==PROFILES,'package targets mismatch')
    for profile in PROFILES:
        d=base/profile;r=json.loads((d/'report.json').read_text());runtime_gate(r,profile,m['source_commit'])
        review=json.loads((d/'review.json').read_text());require(review.get('approved') is True,'human approval missing')
        require(isinstance(review.get('reviewer'),str) and review['reviewer'].strip() and review.get('device_serial'),'reviewer/device missing')
        require(review.get('package_binary_sha256')==m['binary_sha256'] and review.get('report_sha256')==sha(d/'report.json'),'review not bound to this build/report')
        require(all(review.get('checks',{}).get(k) is True for k in CHECKS),'human functional review incomplete')
        files=review.get('evidence',{});require(isinstance(files,dict) and 'display.json' in files and 'timeline.csv' in files,'evidence list incomplete')
        require(any(Path(n).suffix.lower() in ('.mp4','.mov','.jpg','.png') for n in files),'LCD media missing')
        for name,digest in files.items():
            p=d/name;require(not Path(name).is_absolute() and '..' not in Path(name).parts and not p.is_symlink() and p.is_file(),'invalid evidence path')
            require(p.stat().st_size>0 and sha(p)==digest,'evidence hash mismatch: '+name)
        rows=list(csv.DictReader((d/'timeline.csv').open()))
        require(any(int(x['first'])>0 for x in rows),'no visible virtual window transition')
    print('FRAMEWORK_HIL_REVIEW_CONSISTENT both-targets human-attested; not an authenticity proof')
class GateTests(unittest.TestCase):
    def test_missing(self):
        with self.assertRaises(ValueError):runtime_gate({},PROFILES[0],'x')
    def test_headless_rejected(self):
        r=dict(schema=1,profile=PROFILES[0],commit='x',ok=True,error=None,physical_io=False)
        with self.assertRaisesRegex(ValueError,'physical'):runtime_gate(r,PROFILES[0],'x')
    def test_synthetic_rejected(self):
        r=dict(schema=1,profile=PROFILES[0],commit='x',ok=True,error=None,physical_io=True,synthetic=True)
        with self.assertRaisesRegex(ValueError,'physical'):runtime_gate(r,PROFILES[0],'x')
    def test_stale(self):
        with self.assertRaisesRegex(ValueError,'source'):runtime_gate({'schema':1,'commit':'old','profile':PROFILES[0]},PROFILES[0],'new')
    def test_other_profile(self):
        with self.assertRaisesRegex(ValueError,'profile'):runtime_gate({'schema':1,'commit':'x','profile':PROFILES[0]},PROFILES[1],'x')
def main():
    p=argparse.ArgumentParser();p.add_argument('--report',type=Path);p.add_argument('--manifest',type=Path,default=Path('out/framework/device/coffee-framework/manifest.json'));p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:
        result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(GateTests));return 0 if result.wasSuccessful() else 1
    require(a.report is not None,'returned physical reports required');verify(a.report,a.manifest);return 0
if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,ValueError,KeyError,TypeError) as e:print('FRAMEWORK_HIL_PENDING_OR_REJECTED:',e,file=sys.stderr);sys.exit(1)
