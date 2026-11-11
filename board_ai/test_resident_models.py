import json
from pathlib import Path
import sys
import tempfile
import unittest
from resident_models import ResidentPool

FAKE=r'''
import sys,json,os,time
def event(value):
    data=('\n@@RESIDENT@@'+json.dumps(value)+'\n').encode()
    for i in range(0,len(data),3):os.write(1,data[i:i+3])
event(dict(event='ready',load_ms=42,device_free_mb=2000))
for line in sys.stdin:
    obj=json.loads(line)
    if obj.get('op')=='memory':
        event(dict(event='done',id=obj['id'],code=0,device_free_mb=2000));continue
    data='\u4f60\u597d\uff0c\u5de1\u68c0\u52a9\u624b'.encode('utf8')
    for b in data:os.write(1,bytes([b]))
    event(dict(event='done',id=obj['id'],code=0))
'''


class ResidentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.script=self.root/'worker.py';self.script.write_text(FAKE,encoding='utf8')
        self.pool=ResidentPool(self.root,lambda key:([sys.executable,'-u',str(self.script)],self.root))
        self.pool.estimates=dict(small=1000,other=1000,large=4900)

    def tearDown(self):self.pool.close();self.temp.cleanup()

    def run_model(self,key):
        chunks=[]
        result=self.pool.run(key,dict(question='test'),chunks.append)
        self.assertEqual(b''.join(chunks).decode('utf8'),'你好，巡检助手')
        return result

    def test_same_pid_and_zero_load_for_repeated_requests(self):
        first=self.run_model('small');second=self.run_model('small')
        self.assertFalse(first['cache_hit']);self.assertEqual(first['model_load_ms'],42)
        self.assertTrue(second['cache_hit']);self.assertEqual(second['model_load_ms'],0)
        self.assertEqual(first['worker_pid'],second['worker_pid'])

    def test_keep_fitting_models_and_evict_for_large_model(self):
        first=self.run_model('small');self.run_model('other')
        self.assertEqual(len(self.pool.status()['resident_models']),2)
        self.assertEqual(self.run_model('small')['worker_pid'],first['worker_pid'])
        self.run_model('large')
        self.assertEqual([x['model'] for x in self.pool.status()['resident_models']],['large'])


if __name__=='__main__':unittest.main()
