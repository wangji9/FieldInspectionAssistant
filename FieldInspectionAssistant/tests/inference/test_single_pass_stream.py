"""Exercise the real server functions with a fake resident process and no device."""
import ast
import codecs
import json
import logging
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

from answer_policy import constrain, visible_length


def load_server_functions(names, namespace):
    source = Path(__file__).resolve().parents[2] / 'services/inference/server.py'
    module=ast.parse(source.read_text(encoding='utf-8'),filename=str(source))
    definitions=[node for node in module.body if isinstance(node,ast.FunctionDef) and node.name in names]
    assert {node.name for node in definitions}==set(names)
    code=compile(ast.Module(body=definitions,type_ignores=[]),str(source),'exec')
    exec(code,namespace)
    return namespace


class FakeLock:
    def __init__(self): self.releases=0
    def acquire(self,timeout=None): return True
    def release(self): self.releases+=1


class SinglePassStreamingTests(unittest.TestCase):
    def test_resident_last_delta_without_newline_is_flushed(self):
        with tempfile.TemporaryDirectory() as directory:
            seen=[]
            def publish(job,**values):job.update(values);seen.append(dict(values))
            class Pool:
                def run(self,model,payload,consume,*unused):
                    lines='@@JSON@@'+json.dumps({'event':'delta','text':'前半'},ensure_ascii=False)+'\n'+'@@JSON@@'+json.dumps({'event':'delta','text':'完整结尾。'},ensure_ascii=False)
                    data=lines.encode('utf-8')
                    for position in range(0,len(data),7):consume(data[position:position+7])
                    return {'cancelled':False,'decode_tokens':8}
            namespace=dict(codecs=codecs,json=json,time=time,DATA=Path(directory),resident_pool=Pool(),publish=publish,rk_worker=None)
            load_server_functions({'rk1828'},namespace)
            job={'id':'unit','text':'','metrics':{},'created_at':time.time()}
            namespace['rk1828']({'model':'qwen35'},job)
            self.assertEqual(job['text'],'前半完整结尾。')
            self.assertTrue(any(item.get('text')=='前半' for item in seen))
            self.assertEqual(job['metrics']['decode_tokens'],8)

    def test_overlong_streamed_answer_is_not_regenerated_or_erased(self):
        with tempfile.TemporaryDirectory() as directory:
            output=[]; calls=[];profile=FakeLock();submission=FakeLock()
            def publish(job,**values):job.update(values);output.append(dict(values))
            def infer(payload,job):
                calls.append(dict(payload));publish(job,text='这是一个');publish(job,text='这是一个超出字数限制的完整回答。')
            namespace=dict(publish=publish,constrain=constrain,visible_length=visible_length,
                profile_lock=profile,lock=submission,time=time,subprocess=SimpleNamespace(run=lambda *a,**k:None),
                ROOT=Path(directory),DATA=Path(directory),system_status=lambda:{'models':[{'model':'qwen35','ready':True}]},
                RK1828_FILES={'qwen35'},rk1828=infer,native=infer,asr=lambda *_:None,
                snapshot=lambda job:dict(job),json=json,LOG=logging.getLogger('test'))
            load_server_functions({'execute'},namespace)
            job={'id':'unit','text':'','metrics':{},'created_at':time.time()}
            namespace['execute'](job,{'mode':'text','model':'qwen35','question':'问题','answer_limit':8})
            self.assertEqual(len(calls),1)
            self.assertIn('不得超过8字',calls[0]['question'])
            self.assertEqual(job['state'],'done')
            self.assertEqual(job['text'],'这是一个超出字数限制的完整回答。')
            self.assertFalse(job['metrics']['length_compliant'])
            self.assertEqual(job['metrics']['length_attempts'],1)
            self.assertNotIn('',[item['text'] for item in output if 'text' in item])
            self.assertEqual((profile.releases,submission.releases),(1,1))
            self.assertEqual(json.loads((Path(directory)/'unit.json').read_text(encoding='utf-8'))['text'],job['text'])


if __name__=='__main__':unittest.main()
