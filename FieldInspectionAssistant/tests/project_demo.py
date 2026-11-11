"""Generate isolated board QA inputs for the portable pump inspection demo."""

import argparse
import csv
import json
import math
import shutil
import struct
import subprocess
import wave
from pathlib import Path

from PIL import Image, ImageDraw


def make_samples(folder):
    folder.mkdir()
    names = {
        "thermal": "演示_轴承座热像.png",
        "image": "演示_P101测点位置.png",
        "audio": "演示_运行声音.wav",
        "signal": "演示_径向振动.csv",
        "video": "演示_泵组巡查.mp4",
        "text": "演示_异常巡检记录.txt",
        "review": "演示_复检记录.txt",
        "standby": "演示_备用泵交接.txt",
    }
    files = {key: folder / name for key, name in names.items()}
    panel = Image.new("RGB", (640, 400), "#dfe6eb")
    draw = ImageDraw.Draw(panel)
    draw.rectangle((0, 292, 640, 400), fill="#a7b4bc")
    draw.rounded_rectangle((100, 279, 553, 320), radius=7, fill="#56616d")
    draw.rounded_rectangle((123, 169, 350, 273), radius=20, fill="#427589", outline="#355465", width=4)
    for x in range(144, 327, 17):
        draw.line((x, 181, x, 259), fill="#78a5b0", width=4)
    draw.rectangle((342, 200, 422, 241), fill="#6d8391", outline="#4f6270", width=3)
    draw.ellipse((407, 150, 540, 278), fill="#467489", outline="#355465", width=5)
    draw.rectangle((511, 92, 541, 198), fill="#577a8e", outline="#355465", width=3)
    draw.line((378, 218, 378, 99, 464, 99), fill="#b65d29", width=4)
    draw.ellipse((368, 208, 388, 228), outline="#ffcb7b", width=3)
    draw.text((26, 25), "P-101   PUMP / MOTOR ASSEMBLY", fill="#233b4a")
    draw.text((470, 94), "DE BEARING", fill="#7c3916")
    draw.text((26, 366), "TRAINING ILLUSTRATION / SYNTHETIC DATA", fill="#233b4a")
    panel.save(files["image"])

    heat = Image.new("RGB", (640, 400), "#f5f6f8")
    field = Image.new("RGB", (600, 320))
    palette = [(9, 30, 88), (25, 105, 172), (38, 155, 153), (238, 182, 63), (198, 62, 38)]
    for y in range(field.height):
        for x in range(field.width):
            t = math.exp(-(((x - 369) / 80) ** 2 + ((y - 172) / 62) ** 2))
            t += 0.24 * math.exp(-(((x - 234) / 116) ** 2 + ((y - 148) / 88) ** 2))
            color = min(3.999, t * 4)
            low = int(color)
            amount = color - low
            field.putpixel((x, y), tuple(round(a + (b - a) * amount) for a, b in zip(palette[low], palette[low + 1])))
    heat.paste(field, (20, 45))
    draw = ImageDraw.Draw(heat)
    draw.text((20, 19), "P-101 / DE BEARING   THERMAL TRAINING MAP", fill="#253c4f")
    draw.line((379, 217, 399, 217), fill="white", width=2)
    draw.line((389, 207, 389, 227), fill="white", width=2)
    draw.text((405, 198), "67.4 C", fill="white")
    draw.text((20, 377), "SIMULATED FRAME  /  NOT A FIELD MEASUREMENT", fill="#253c4f")
    heat.save(files["thermal"])

    rate = 16000
    with wave.open(str(files["audio"]), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        samples = []
        for index in range(4 * rate):
            time = index / rate
            gain = min(1, time * 10, (4 - time) * 10)
            value = gain * (0.23 * math.sin(2 * math.pi * 49.7 * time) + 0.065 * math.sin(2 * math.pi * 99.4 * time))
            samples.append(round(value * 32767))
        output.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    with files["signal"].open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("time_s", "simulated_velocity_mm_s"))
        for index in range(2048):
            time = index / 1024
            value = 6.4 * math.sqrt(2) * math.sin(2 * math.pi * 49.7 * time)
            writer.writerow((f"{time:.7f}", f"{value:.5f}"))
    subprocess.run(
        ["ffmpeg", "-nostdin", "-v", "error", "-loop", "1", "-i", str(files["image"]),
         "-vf", "zoompan=z='min(zoom+0.002,1.12)':x='iw/2-iw/zoom/2':y='ih/2-ih/zoom/2':d=1:s=640x400:fps=24",
         "-t", "4", "-c:v", "libx264", "-threads", "2", "-pix_fmt", "yuv420p", str(files["video"])],
        check=True,
    )
    files["text"].write_text(
        "培训演示，以下均为模拟数据，不作为真实检测结果。\n"
        "任务：循环水泵月度巡检。设备 P-101，测点驱动端轴承座，工况为稳定运行、转速 2982 r/min。\n"
        "初检：热像模拟峰值 67.4 °C，径向振动模拟有效值 6.4 mm/s，主要分量 49.7 Hz。\n"
        "本案例内部复查线为 60 °C 和 4.5 mm/s，仅用于演示。登记异常并建议在同工况下复测，检查探头固定、负荷和对中记录。"
        "单靠这些模拟证据不能判定轴承故障。\n", encoding="utf-8",
    )
    files["review"].write_text(
        "培训演示，以下均为模拟数据。\n"
        "整改后复检：确认测量方向和工况，模拟峰值降至 51.2 °C，振动有效值 2.9 mm/s。"
        "建议留存两次原始资料并继续趋势观察。正式关闭需由现场人员确认。\n", encoding="utf-8",
    )
    files["standby"].write_text(
        "培训演示。备用 P-102 停机待命，核对设备铭牌和交接状态。未运行，不进行轴承温升或运行振动判定。\n",
        encoding="utf-8",
    )
    return files


def use_public_media(files, source):
    folder = files["image"].parent
    public_names = {
        "image": ("pump-frame.jpg", "公开实录_齿轮泵视频截图.jpg"),
        "video": ("pump-cavitation.mp4", "公开实录_齿轮泵气蚀试验.mp4"),
        "audio": ("pump-motor.wav", "公开实录_独立泵声.wav"),
    }
    for kind, (original, name) in public_names.items():
        files[kind] = folder / name
        shutil.copyfile(source / original, files[kind])
    files["matrix"] = folder / "模拟_轴承座80x60-celsius.csv"
    with files["matrix"].open("w", encoding="utf-8", newline="") as output:
        rows = csv.writer(output)
        for y in range(60):
            rows.writerow(f"{31.0 + 36.4 * math.exp(-((x-49)/8.0)**2 - ((y-31)/7.5)**2):.4f}" for x in range(80))
    files["signal"] = folder / "派生_泵声音量包络.csv"
    with wave.open(str(files["audio"]), "rb") as sound, files["signal"].open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("time_s", "audio_envelope_rms_fs"))
        window = sound.getframerate() // 100
        for index in range(sound.getnframes() // window):
            samples = struct.unpack(f"<{window}h", sound.readframes(window))
            rms = math.sqrt(sum((sample / 32768) ** 2 for sample in samples) / window)
            writer.writerow((f"{index / 100:.2f}", f"{rms:.7f}"))
    files["text"].write_text(
        "培训演示。公开视频与泵声来自不同设备，时间轴为演示编排，并非现场同步测量。"
        "图像取自同一公开试验视频，包络取自同一录音，均非独立佐证。\n"
        "视频：Sir Balefire，齿轮泵气蚀台架试验，CC0 1.0。"
        "泵声：ezwa / PDSounds，公有领域。\n"
        "演示场景：P-101 驱动端轴承座。模拟温度矩阵峰值 67.4 °C；模拟巡检录入振动 6.4 mm/s。"
        "样例复查线 60 °C 和 4.5 mm/s，只用于培训。模拟读数与公开视频、声音没有实物关联。\n"
        "视频来源：https://commons.wikimedia.org/wiki/File:Cavitation_in_a_gear_pump.ogv\n"
        "音频来源：https://commons.wikimedia.org/wiki/File:Pump.ogg\n", encoding="utf-8",
    )
    return files


def make_steps(root, files, hardware, public=False):
    steps = []
    directory = root / "work"
    exports = root / "exports"
    exports.mkdir()
    (exports / "copies").mkdir()
    directory.mkdir()
    name = "泵组多模态巡检演示" if public else "设备巡检演示"
    description = ("公开齿轮泵试验、独立泵声与模拟巡检数据的培训案例。统一演示时间轴，非现场同步测量。"
                   if public else "循环水泵 P-101 轴承座温升与振动复检。现场资料为培训样本，AI 分析由板端模型实际运行。")
    project = "App.projects"
    source = directory / (name + ".miproj")

    def js(value):
        return json.dumps(value, ensure_ascii=False)

    def step(label, expression, **timing):
        steps.append(dict(name=label, expression=expression, **timing))

    def check(label, expression):
        step(label, f"if (!({expression})) throw new Error({js(label)}); true")

    def call(label, expression, after=""):
        step(label, f"var r={expression}; if (!r.ok) throw new Error(r.error); {after} true")

    step("empty-overview", "auditWindow.width=1536; auditWindow.height=864; auditWindow.showNormal(); "
         "if (!App.session.setupAdministrator('project-demo-qa','IsolatedTrainingWorkspace1',true)) "
         "throw new Error(App.session.message); App.nav.push('projects'); true")
    call("create", f"{project}.createProject({js(name)}, {js(directory.as_uri())})")
    call("save", f"{project}.saveProject({js(name)}, {js(description)})")
    call("register-pump", f"{project}.addDevice('P-101 循环水泵')")
    call("register-standby", f"{project}.addDevice('P-102 备用泵')")
    task_name = "演示：P-101 月度巡检"
    asset = "App.assets.findByCode('DEMO-P101').asset.id"
    task = f"App.inspection.tasks('').find(function(t) {{return t.name === {js(task_name)}}}).id"
    record = f"App.inspection.recordFor({task},{asset}).recordId"
    defect_title = "演示：轴承座温升与振动偏高"
    defect = f"App.defects.defects('').find(function(d) {{return d.title === {js(defect_title)}}}).id"
    template = {"name": "泵组巡检演示标准", "version": 1, "definition": {"items": [
        {"key": "temperature", "name": "轴承座温度", "type": "measure", "unit": "°C", "max": 60},
        {"key": "vibration", "name": "径向振动有效值", "type": "measure", "unit": "mm/s", "max": 4.5},
    ]}}
    call("operations-asset", "App.assets.createAsset('DEMO-P101','演示循环水泵','演示泵房 A','P-101')")
    point_name = "驱动端轴承座"
    component = f"App.workspace.options('components',{{asset_id:{asset}}})[0].id"
    point_id = f"App.workspace.options('points',{{asset_id:{asset}}}).find(row => row.name === {js(point_name)}).id"
    call("operations-component", f"App.workspace.save('components',{{asset_id:{asset},name:'驱动端轴承',position:'泵组驱动端'}})")
    call("operations-point", f"App.workspace.save('points',{{component_id:{component},code:'DE-BRG',"
         f"name:{js(point_name)},position:'轴承座外表面',operating_condition:'模拟稳定运行'}})")
    for metric, unit, maximum in (("轴承座温度", "°C", "60"), ("径向振动有效值", "mm/s", "4.5")):
        call("operations-baseline-" + ("temp" if unit == "°C" else "vibration"), f"App.workspace.save('baselines',{{point_id:{point_id},"
             f"metric:{js(metric)},unit:{js(unit)},maximum:{js(maximum)},operating_condition:'模拟稳定运行',reference:'培训样例基准'}})")
    call("operations-task", "App.workspace.save('tasks', {name:" + js(task_name)
         + f",asset_id:{asset},assignee_id:App.session.userId,planned_at:new Date().toISOString(),"
         + "template_snapshot:" + js(json.dumps(template, ensure_ascii=False)) + "})")
    call("operations-start", f"App.inspection.beginInspection({asset},{task})")
    check("operations-incomplete-blocked", f"!App.inspection.completeTask({task}).ok")
    for key, value in (("temperature", "67.4"), ("vibration", "6.4")):
        call(f"operations-{key}", f"App.inspection.saveItem({record}, {js(key)}, 'normal', {js(value)}, '模拟读数')",
             "if (!r.abnormal) throw new Error('超出样例阈值应标为异常');")
    for key, metric, unit, value in (("temp","轴承座温度","°C","67.4"), ("vibration","径向振动有效值","mm/s","6.4")):
        call(f"operations-reading-{key}", f"App.workspace.linkEvidence({{record_id:{record},point_id:{point_id},"
             f"kind:'reading',title:{js('模拟'+metric)},metadata:{{metric:{js(metric)},value:{js(value)},unit:{js(unit)},"
             "operating_condition:'模拟稳定运行'}})")
    check("operations-unlinked-blocked", f"!App.inspection.completeTask({task}).ok")
    call("operations-defect", f"App.defects.createDefect({record},{asset},{js(defect_title)},'同工况复测，检查对中','high','temperature')")
    call("operations-defect-signal", f"App.defects.createDefect({record},{asset},'演示：径向振动异常','复测与温升异常的关联','medium','vibration')")
    call("operations-attach", f"App.workspace.linkEvidence({{record_id:{record},kind:'conclusion',"
         "title:'初检与复查计划',content:'培训演示。复测安装状态、温度和振动。'})")
    call("operations-submit", f"App.inspection.completeTask({task})")
    call("operations-assign", f"App.defects.assign({defect},App.session.userId,'','演示整改')")
    call("operations-repair", f"App.defects.start({defect},'同工况复检')")
    call("operations-feedback", f"App.defects.submitFeedback({defect},'模拟复测 51.2 °C，2.9 mm/s')")
    call("operations-return", f"App.defects.review({defect},false,'复核退回，补充复测读数')")
    check("returned-state", f"App.workflow.detail('defects',{defect}).selected_defect.returned === true")
    step("returned-progress", f"App.nav.push('operations',{{view:'defects',asset_id:{asset},source_id:{defect}}}); true",
         captureWindow="inspectionDetailWindow")
    call("operations-resubmit", f"App.defects.submitFeedback({defect},'补充模拟复测记录 51.2 °C，2.9 mm/s')")
    call("operations-review", f"App.defects.review({defect},true,'温升缺陷演示闭环，另一条振动缺陷待复核')")
    step("closed-progress", "true", captureWindow="inspectionDetailWindow")
    formal_json = exports / "演示_现场执行与复核.json"
    formal_pdf = exports / "泵组现场执行与复核.pdf"
    call("operations-confirm", f"App.workspace.confirmReport({record},'培训演示。温升异常已按样例复检，径向振动缺陷仍需跟踪。','',0)")
    report = f"App.workspace.report({record}).id"
    call("operations-export-json", f"App.workspace.exportReport({report},{js(str(formal_json))},'json')")
    call("operations-export-pdf", f"App.workspace.exportReport({report},{js(str(formal_pdf))},'pdf')")
    kinds = ["image", "thermal", "audio", "signal", "video", "text", "review", "standby"]
    if public:
        kinds.append("matrix")
    for index, kind in enumerate(kinds):
        other = kind == "standby"
        point = "驱动端轴承座" if not other else "设备铭牌"
        actual_kind = "text" if kind in {"review", "standby"} else "thermal" if kind == "matrix" else kind
        time = f"2026-11-08T10:00:{index * 4:02}+08:00" if index < 6 else f"2026-11-08T10:15:{index:02}+08:00"
        if public and kind not in {"review", "standby"}:
            seconds = {"image":3, "thermal":4, "matrix":4, "audio":6, "signal":6, "video":0, "text":12}[kind]
            time = f"2026-11-08T10:00:{seconds:02}.250+08:00"
        call(f"attach-{kind}", f"{project}.importEvidence({js(files[kind].as_uri())},"
             f" {project}.devices[{int(other)}].id, {js(point)}, {js(actual_kind)}, {js(time)})")

    def evidence(kind):
        return f"{project}.evidence.find(function(row) {{return row.title === {js(files[kind].name)}}})"

    check("negative-one-modality", f"!{project}.fuse({evidence('standby')}.id).ok")
    call("fuse", f"{project}.fuse({evidence('thermal')}.id)")
    check("fusion-deduplicates", f"{project}.fuse({evidence('thermal')}.id).id === {project}.events[0].id && {project}.events.length === 1")
    check("evidence-preview", f"{project}.evidenceFile({evidence('thermal')}.id).image"
          f" && {project}.evidenceFile({evidence('text')}.id).text.includes('67.4')")
    call("attach-inspection", f"{project}.linkInspection({project}.events[0].id,{record})")
    call("attach-defect", f"{project}.linkDefect({project}.events[0].id,{defect})")
    timeline = f"{project}.eventTimeline({project}.events[0].id)"
    check("aligned-timeline", f"{timeline}.length === {7 if public else 6} && {timeline}.every(row => row.timeUtc.endsWith('Z') && row.source === 'declared_offset')")
    check("measured-evidence", f"{timeline}.some(row => row.kind === 'audio' && row.metrics.rms > 0)"
          f" && {timeline}.some(row => row.kind === 'signal' && row.metrics.sample_count > 0)")
    if public:
        check("measured-temperature-matrix", f"{timeline}.some(row => row.metrics.source === 'temperature_matrix' && row.metrics.maximum === 67.4)")
    step("analysis-service", "App.projectInference.refresh(); true", until="App.projectInference.ready", timeout=12000)
    step("rk1828-analysis", f"App.nav.push('projectDiagnosis',{{event_id:{project}.events[0].id}}); App.projectInference.analyze({project}.events[0].id); true",
         until=f"{project}.findingForEvent({project}.events[0].id).length > 20", timeout=270000)
    check("persisted-video-observation", f"{timeline}.some(row => row.kind === 'video' && row.observation.frame_offsets_ms.length === 3)")
    step("event-graph", f"App.nav.push('projectGraph',{{event_id:{project}.events[0].id,device_id:{project}.devices[0].id}}); true")
    check("graph-relations", f"['record:', 'defect:', 'finding:'].every(prefix => "
          f"{project}.evidenceGraph('',{project}.events[0].id).nodes.some(node => node.id.startsWith(prefix)))")
    call("attach-confirmed-report", f"{project}.importEvidence({js(formal_json.as_uri())},"
         f" {project}.devices[0].id,'驱动端轴承座','text','2026-11-08T10:17:00+08:00')")
    call("markdown", f"{project}.exportReport({project}.events[0].id)", "App.nav.push('reports');")
    call("pdf", f"{project}.exportReportPdf({project}.reports[0].id, {js((exports / '泵组巡检演示报告.pdf').as_uri())})")
    for template_id, filename in (("brief", "巡检事件摘要.pdf"), ("followup", "缺陷整改跟踪报告.pdf")):
        call("pdf-" + template_id, f"{project}.exportReportPdf({project}.reports[0].id, {js((exports / filename).as_uri())},{js(template_id)})")
    check("reject-invalid-import", f"!{project}.importProject({js(exports.as_uri())}).ok && {project}.project.name === {js(name)}")
    step("close", f"{project}.closeProject(); true")
    call("reopen", f"{project}.openProject({js(source.as_uri())})", "App.nav.push('projects');")
    check("complete-overview", f"{project}.evidence.length === {10 if public else 9} && {project}.reports.length === 1")
    check("observations-survive-reopen", f"{timeline}.some(row => row.kind === 'video' && row.observation.frame_offsets_ms.length === 3)")
    check("report-survives-reopen", f"{project}.reportHtml({project}.reports[0].id,'followup').includes('缺陷整改跟踪报告')")
    step("compact-overview", "auditWindow.width=980; auditWindow.height=620; true")
    step("overview-full", "auditWindow.width=1536; auditWindow.height=864; true")
    step("overview-evidence-bar", "true", tapObject="projectEvidenceBar-thermal")
    check("overview-evidence-route", "App.nav.page === 'projectEvidence'")
    call("export-project", f"{project}.copyProject({js(exports.as_uri())}, '', '', true)")
    call("import-project", f"{project}.importProject({js(source.as_uri())})")
    check("imported-evidence", f"{project}.evidence.length === {10 if public else 9} && {project}.reports.length === 1"
          f" && {project}.project.path !== {js(str(source))}")
    call("save-as", f"{project}.copyProject({js((exports / 'copies').as_uri())}, '另存演示', '副本验收', false)")
    step("wav-analysis", f"App.nav.push('analysis', {{analysis_tab:0}}); App.analysis.refresh(); true")
    step("wav-import", f"App.analysis.importFile({project}.evidenceFile({evidence('audio')}.id).localPath); true",
         until="App.analysis.recordings.length > 0 && !App.analysis.recordingBusy", timeout=25000)
    step("wave-export", f"App.analysis.exportFile({js(str(exports / 'wave.csv'))}, 'csv', 'full'); true",
         until="!App.analysis.exportBusy && App.analysis.exportResult.ok === true", timeout=25000)
    if hardware:
        step("thermal-live", "auditWindow.startCapture(); true", until="auditWindow.thermal.hasFrame && auditWindow.thermal.fps > 1", timeout=16000)
        step("thermal-fps", "auditWindow.thermal.fps")
        step("thermal-export", f"var p=auditWindow.thermal.saveFrame({js(exports.as_uri())}); if (!p) throw new Error(auditWindow.thermal.status); p")
        check("thermal-pause", "auditWindow.thermal.togglePause() === undefined && auditWindow.thermal.paused")
        step("thermal-stop", "auditWindow.stopCapture(); true", until="!auditWindow.thermal.connected", timeout=5000)
        step("camera-live", f"App.camera.saveDirectory={js(str(exports))}; "
             "App.nav.push('camera'); App.camera.configure(1280,720,30); "
             "App.evidence.openCamera('/dev/video1'); true", until="App.camera.hasFrame", timeout=10000)
        step("camera-photo", "if(!App.camera.savePhotoAsync()) throw new Error(App.camera.status); true",
             until="App.camera.lastPhoto.length > 0 && !App.camera.photoSaving", timeout=10000)
        step("camera-video", "if(!App.camera.startVideo(15, 5, 4)) throw new Error(App.camera.status); true",
             until="App.camera.recordingSeconds >= 2", timeout=10000)
        step("camera-save-video", "App.camera.stopVideo(); true",
             until="App.camera.lastVideo.length > 0 && !App.camera.finalizing", timeout=15000)
        step("camera-stop", "App.evidence.closeCamera(); true", until="!App.evidence.cameraConnected", timeout=8000)
        step("audio-play", f"App.speech.playAudio({js(str(files['audio']))}); true",
             until="App.speech.message === '播放完成'", timeout=15000)
        step("raw-mic-discover", "App.analysis.refresh(); true",
             until="App.analysis.devices.some(function(d){return d.id === 'ListenGo_raw_six'})", timeout=8500)
        step("raw-mic-start", "App.analysis.start('ListenGo_raw_six'); true",
             until="App.analysis.summary.capture_frames >= 16000 && App.analysis.summary.channels === 6", timeout=10000)
        step("raw-mic-stop", "App.analysis.stop(); true", until="!App.analysis.running", timeout=5000)
    if public:
        call("restore-demo-project", f"{project}.openProject({js(source.as_uri())})")
        step("demo-landing", "App.nav.push('projects'); true")
    step("report-preview", "auditWindow.showReport(); true")
    (root / "steps.json").write_text(json.dumps(steps, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--hardware", action="store_true")
    parser.add_argument("--public-media", type=Path, help="directory with converted video, extracted photo and PCM WAV")
    args = parser.parse_args()
    files = make_samples(args.root.resolve() / "samples")
    if args.public_media:
        files = use_public_media(files, args.public_media)
    make_steps(args.root.resolve(), files, args.hardware, bool(args.public_media))
