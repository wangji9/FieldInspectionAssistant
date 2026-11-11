"""Persistent RK1828 workers with measured memory-aware LRU retention.

Requests remain serialized by server.py. This lock also covers background
preloading, so two clients never write into one model session at the same time.
"""
import json
import os
import queue
import signal
import subprocess
import threading
import time
import uuid
from contextlib import nullcontext
from pathlib import Path

MARKER=b'\n@@RESIDENT@@'


class ResidentProcess:
    def __init__(self, args, cwd, log_path):
        env=dict(os.environ, LD_LIBRARY_PATH=str(Path(cwd)/'lib')+':/usr/lib')
        self.proc=subprocess.Popen(args, cwd=cwd, env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.queue=queue.Queue()
        self.buffer=b''
        self.result={}
        self.log_path=Path(log_path)
        def read():
            try:
                while True:
                    chunk=os.read(self.proc.stdout.fileno(),4096)
                    if not chunk:break
                    self.queue.put(chunk)
            finally:self.queue.put(None)
        self.reader=threading.Thread(target=read,daemon=True);self.reader.start()

    def messages(self, timeout=600, cancelled=lambda:False):
        deadline=time.monotonic()+timeout
        cancellation=None
        while time.monotonic()<deadline:
            if cancelled() and cancellation is None:
                cancellation=time.monotonic()
                if self.proc.poll() is None:self.proc.send_signal(signal.SIGUSR1)
            if cancellation and time.monotonic()-cancellation>3:
                raise TimeoutError('模型未及时响应停止，已重置工作进程')
            index=self.buffer.find(MARKER)
            if index>=0:
                if index:
                    data=self.buffer[:index];self.buffer=self.buffer[index:]
                    yield 'data',data
                    continue
                end=self.buffer.find(b'\n',len(MARKER))
                if end>=0:
                    data=self.buffer[len(MARKER):end];self.buffer=self.buffer[end+1:]
                    yield 'control',json.loads(data)
                    continue
            elif len(self.buffer)>=len(MARKER):
                # Keep only a possible split control prefix; native ASR token
                # bytes reach the existing incremental UTF-8 decoder promptly.
                data=self.buffer[:-len(MARKER)+1];self.buffer=self.buffer[-len(MARKER)+1:]
                yield 'data',data
                continue
            try:chunk=self.queue.get(timeout=min(.2,max(.001,deadline-time.monotonic())))
            except queue.Empty:continue
            if chunk is None:
                raise RuntimeError('常驻模型进程退出：'+str(self.proc.poll())+'；请检查 '+str(self.log_path))
            self.buffer+=chunk
        raise TimeoutError('常驻模型操作超时')

    def start(self, timeout=180):
        with self.log_path.open('ab') as log:
            for kind,value in self.messages(timeout=timeout):
                if kind=='data':log.write(value);log.flush()
                elif value.get('event')=='ready':return value
        raise RuntimeError('模型没有返回就绪事件')

    def request(self, payload, cancelled=lambda:False, timeout=600):
        rid=uuid.uuid4().hex
        payload=dict(payload,id=rid)
        self.proc.stdin.write((json.dumps(payload,ensure_ascii=False)+'\n').encode('utf-8'))
        self.proc.stdin.flush()
        for kind,value in self.messages(cancelled=cancelled,timeout=timeout):
            if kind=='data':yield value
            elif value.get('event')=='done':
                if value.get('id')!=rid:raise RuntimeError('常驻模型响应编号不匹配')
                self.result=value
                return

    def memory(self):
        for _ in self.request(dict(op='memory')):pass
        return self.result

    def close(self):
        if self.proc.poll() is None:
            try:
                self.proc.stdin.close();self.proc.wait(timeout=8)
            except (OSError,subprocess.TimeoutExpired):
                self.proc.kill();self.proc.wait(timeout=5)
        if not self.proc.stdin.closed:self.proc.stdin.close()
        self.reader.join(timeout=2)
        self.proc.stdout.close()


class ResidentPool:
    # Conservative initial estimates, replaced by actual device allocation deltas.
    estimates={'qwen35':2900,'qwen25vl':4900,'qwen3_asr':2000,'qwen3_asr_online':2000}

    def __init__(self, root, specs, profile=None):
        self.root=Path(root);self.specs=specs;self.profile=profile or nullcontext
        self.lock=threading.RLock();self.state_lock=threading.Lock()
        self.workers={};self.loading='';self.last_error='';self.prepare_generation=0;self.memory_info={}

    def status(self):
        with self.state_lock:
            return dict(resident_models=[dict(model=k,pid=v['worker'].proc.pid,load_ms=v['load_ms'],
                        memory_mb=v.get('memory_mb'),requests=v.get('requests',0))
                        for k,v in self.workers.items() if v['worker'].proc.poll() is None],
                        loading_model=self.loading,preload_error=self.last_error,device_memory=self.memory_info)

    def evict(self,key):
        with self.state_lock:entry=self.workers.pop(key,None)
        if entry:entry['worker'].close()

    def release_idle(self, keys, age):
        if not self.lock.acquire(False):
            return
        try:
            now = time.monotonic()
            for key in keys:
                entry = self.workers.get(key)
                if entry and now - entry['last_used'] >= age:
                    self.evict(key)
        finally:
            self.lock.release()

    def ensure(self,key,callback=lambda message:None):
        entry=self.workers.get(key)
        if entry and entry['worker'].proc.poll() is None:
            entry['last_used']=time.monotonic();return entry,True
        if entry:self.evict(key)
        # Video model startup can briefly exceed its steady-state allocation.
        for loaded in list(self.workers):
            if key.startswith('qwen3_video') or loaded.startswith('qwen3_video'): self.evict(loaded)
        required=self.estimates.get(key,4900)
        free=None
        while self.workers:
            latest=max(self.workers,key=lambda k:self.workers[k]['last_used'])
            memory=self.workers[latest]['worker'].memory()
            with self.state_lock:self.memory_info={k:v for k,v in memory.items() if k.startswith('device_')}
            free=memory.get('device_free_mb')
            if free is not None and free>=required+32:break
            oldest=min(self.workers,key=lambda k:self.workers[k]['last_used'])
            callback('正在释放闲置模型以准备 '+key)
            self.evict(oldest)
        args,cwd=self.specs(key)
        with self.state_lock:self.loading=key;self.last_error=''
        callback('首次加载模型 '+key+'，加载完成后保持驻留')
        try:
            alone=not self.workers
            for attempt in range(2):
                worker=ResidentProcess(args,cwd,self.root/(key+'-resident.log'))
                try:
                    ready=worker.start(timeout=55) if key.startswith('qwen3_video') else worker.start()
                    break
                except Exception:
                    worker.close()
                    if attempt or not self.workers:raise
                    for old in list(self.workers):self.evict(old)
            remaining=ready.get('device_free_mb')
            with self.state_lock:self.memory_info={k:v for k,v in ready.items() if k.startswith('device_')}
            total=ready.get('device_total_mb')
            allocated=(max(0,total-remaining) if alone and total is not None and remaining is not None
                       else max(0,free-remaining) if free is not None and remaining is not None else None)
            if allocated:self.estimates[key]=allocated+32
            entry=dict(worker=worker,load_ms=ready['load_ms'],last_used=time.monotonic(),requests=0,memory_mb=allocated)
            with self.state_lock:self.workers[key]=entry
            return entry,False
        except Exception as exc:
            with self.state_lock:self.last_error=str(exc)
            raise
        finally:
            with self.state_lock:self.loading=''

    def run(self,key,payload,on_chunk,callback=lambda message:None,cancelled=lambda:False,on_process=lambda p:None):
        waited=time.monotonic()
        with self.lock:
            wait_ms=(time.monotonic()-waited)*1000
            entry,hit=self.ensure(key,callback)
            process=entry['worker'];on_process(process.proc)
            if cancelled():
                on_process(None)
                return dict(cancelled=True,cache_hit=hit,model_load_ms=0 if hit else entry['load_ms'])
            callback('使用已驻留模型直接推理' if hit else '模型已就绪，正在推理')
            try:
                chunks=process.request(payload,cancelled,timeout=12) if key.startswith('qwen3_video') else process.request(payload,cancelled)
                for chunk in chunks:on_chunk(chunk)
                result=process.result
                if result.get('code',0) and not result.get('cancelled'):
                    raise RuntimeError('常驻模型推理失败，代码 '+str(result['code']))
                entry['requests']+=1;entry['last_used']=time.monotonic()
                return dict(cancelled=result.get('cancelled',False),cache_hit=hit,
                            model_load_ms=0 if hit else entry['load_ms'],preload_wait_ms=wait_ms,worker_pid=process.proc.pid)
            except Exception:
                self.evict(key)
                if cancelled():return dict(cancelled=True,cache_hit=False,model_load_ms=0)
                raise
            finally:on_process(None)

    def prepare(self,key,also_asr=False):
        with self.state_lock:self.prepare_generation+=1;generation=self.prepare_generation
        def warm():
            try:
                with self.profile():
                    with self.lock:
                        if generation!=self.prepare_generation:return
                        self.ensure(key)
                        if also_asr and generation==self.prepare_generation and key=='qwen35':
                            self.ensure('qwen3_asr')
            except Exception as exc:
                with self.state_lock:self.last_error=str(exc)
        threading.Thread(target=warm,daemon=True).start()

    def close(self,cancel_prepares=False):
        with self.lock:
            if cancel_prepares:
                with self.state_lock:self.prepare_generation+=1
            for key in list(self.workers):self.evict(key)
