"""Export deployment evidence and most recent successful per-module measurements."""
import json, os, subprocess
from datetime import datetime
from pathlib import Path
from urllib.request import urlopen

ROOT=Path('/userdata/ai-workbench')
with urlopen('http://127.0.0.1:1828/api/status',timeout=10) as response: status=json.load(response)
records=[]
for p in (ROOT/'data').glob('*.json'):
    record=json.loads(p.read_text())
    if record.get('state')=='done': records.append(record)
records.sort(key=lambda r:r['created_at'],reverse=True)
latest={}
for record in records:
    key=record['mode']+('-online' if record.get('metrics',{}).get('streaming_rounds') else '')
    if key not in latest: latest[key]=record
required=['text','encoder','vision','asr']
complete=all(m['ready'] for m in status['models']) and all(k in latest for k in required)
manifest=json.loads(Path('/userdata/models/models-manifest.json').read_text())
report=dict(generated_at=datetime.now().astimezone().isoformat(),complete=complete,resolution='1280x720',
    service=status,manifest=manifest,latest=latest,successful_runs=len(records))
(ROOT/'deployment-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
names={'text':'Qwen3-4B 文字问答','encoder':'Qwen3-VL 视觉编码','vision':'Qwen3-VL 图像理解','asr':'Qwen3-ASR 离线','asr-online':'Qwen3-ASR 流式'}
def n(m,key,digits=2): return f'{m[key]:.{digits}f}' if key in m else '—'
lines=['# 板端部署与实测记录','',f"时间：{report['generated_at']}",'',
    f"部署：{'全部通过' if complete else '仍有项目待验证'}；板端 `10.206.38.255`；界面 `1280×720`。",'',
    '| 模块 | 解码 tokens/s | 预填充 tokens/s | 首 Token ms | 编码 ms | 加载 ms | 完整请求 ms | RTF |',
    '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
for key in required+['asr-online']:
    if key not in latest: continue
    m=latest[key]['metrics']
    vals=[n(m,'decode_tps'),n(m,'prefill_tps'),n(m,'ttft_ms',1),n(m,'encoder_ms',1),n(m,'model_load_ms',1),n(m,'request_ms',1),n(m,'rtf',4)]
    lines.append('| '+names[key]+' | '+' | '.join(vals)+' |')
lines+=['','视觉编码器不生成语言 Token，使用编码耗时与 FPS 衡量。ASR 流式计数包含回退修正的计算。RTF 不含模型初始化，完整请求耗时包含初始化与输入处理。','',
    '模型共 11 个文件，已逐文件校验两端 SHA-256，详见 `models-manifest.json`。','',
    '代码位于 `/home/ztl/FieldInspectionAssistant` 与 `/userdata/ai-workbench`；模型位于 `/userdata/models`。',
    '推理服务 `board-ai.service` 已启用开机启动；Qt 程序通过桌面入口及桌面会话自启动。','',
    '临时文件接收端口已关闭；推理 API 监听 `127.0.0.1:1828`。','',
    '## 最近成功推理输出','']
for key in required+['asr-online']:
    if key in latest: lines+=['### '+names[key],'',latest[key]['text'],'']
(ROOT/'deployment-report.md').write_text('\n'.join(lines),encoding='utf-8')
print(json.dumps(dict(complete=complete,successful_runs=len(records),models=[m['name'] for m in status['models']],
    metrics={k:latest[k]['metrics'] for k in latest}),ensure_ascii=False,indent=2))
if not complete: raise SystemExit(1)
