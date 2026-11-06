"""Reject malformed GPU replay captures before they enter a kernel build."""
from pathlib import Path
import json,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class FixtureValidation(unittest.TestCase):
    def run_capture(self,change=None):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);raw=bytearray(16384)
            struct.pack_into('<I',raw,16,0x10109)
            struct.pack_into('<64I',raw,0x800,*([0xdeadbeef]*64))
            meta={'jc':0x2000000,'requirements':0,'bos':[{'handle':1,'va':0x2000000,'size':len(raw)}]}
            if change:change(meta,raw)
            (p/'submit-1.json').write_text(json.dumps(meta));(p/'submit-1-bo-1.bin').write_bytes(raw)
            r=subprocess.run([sys.executable,str(ROOT/'tools/make_gpu_fixture.py'),str(p),
                              '--output-va','0x2000800','--output',str(p/'fixture.h')],capture_output=True)
            return r.returncode
    def test_valid_compute(self):self.assertEqual(self.run_capture(),0)
    def test_fragment_rejected(self):self.assertNotEqual(self.run_capture(lambda m,r:m.update(requirements=1)),0)
    def test_non_compute_rejected(self):self.assertNotEqual(self.run_capture(lambda m,r:struct.pack_into('<I',r,16,0x10105)),0)
    def test_chained_job_rejected(self):self.assertNotEqual(self.run_capture(lambda m,r:struct.pack_into('<Q',r,24,0x2000100)),0)
    def test_wrong_output_rejected(self):self.assertNotEqual(self.run_capture(lambda m,r:r.__setitem__(0x800,0)),0)
    def test_unaligned_map_rejected(self):self.assertNotEqual(self.run_capture(lambda m,r:m['bos'][0].update(va=0x2000001)),0)
    def test_overlapping_map_rejected(self):self.assertNotEqual(self.run_capture(lambda m,r:m['bos'].append(dict(m['bos'][0]))),0)
if __name__=='__main__':unittest.main()
