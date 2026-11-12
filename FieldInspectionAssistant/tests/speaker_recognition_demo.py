"""Prepare isolated speaker enrollment and recognition checks for the ARM64 board."""
import argparse
import json
import wave
from pathlib import Path


def build_steps(directory, samples):
    a1 = samples / "speaker1_a_cn_16k.wav"
    a2 = samples / "speaker1_b_cn_16k.wav"
    a_en = samples / "speaker1_a_en_16k.wav"
    b = samples / "speaker2_a_cn_16k.wav"
    joined = directory / "handoff.wav"
    with wave.open(str(joined), "wb") as audio:
        audio.setparams((1, 2, 16000, 0, "NONE", ""))
        for path in (a1, b, a2):
            with wave.open(str(path), "rb") as clip:
                assert (clip.getnchannels(), clip.getsampwidth(), clip.getframerate()) == (1, 2, 16000)
                audio.writeframes(clip.readframes(clip.getnframes()))
            if path != a2:
                audio.writeframes(b"\0" * (2 * 56000))
    out = directory / "speaker-results.json"
    service = "App.speakerRecognition"
    steps = []

    def literal(value):
        return json.dumps(value, ensure_ascii=False)

    def step(name, expression, until=None, timeout=30000):
        item = {"name": name, "expression": expression}
        if until:
            item.update(until=until, timeout=timeout)
        steps.append(item)

    def check(name, expression):
        step(name, "if (!(" + expression + ")) throw new Error(" + literal(name) + "); true")

    def queued(name, expression):
        step(name, f"if (!({expression})) throw new Error({service}.status); true", f"!{service}.busy")

    def identify(path, transcript=False, threshold=".77"):
        flag = "true" if transcript else "false"
        return f"{service}.analyze({literal(path.as_uri())},0,30,{threshold},{flag})"

    def enroll(path, name, lang, profile="''"):
        return f"{service}.enroll({literal(path.as_uri())},0,30,{profile},{literal(name)},{literal(lang)})"

    step("startup", "auditWindow.width=1536; auditWindow.height=864; auditWindow.showNormal(); "
         "if (!App.session.setupAdministrator('speaker-test','IsolatedSpeakerTest1',true)) throw new Error(App.session.message); "
         "App.nav.push('analysis',{analysis_tab:5}); true", f"{service}.ready")
    check("whisper-assets", f"{service}.whisperReady")
    queued("enroll-a", enroll(a1, "示例甲", "zh"))
    check("enrollment-a", f"{service}.profiles.length === 1 && {service}.profiles[0].samples === 1")
    queued("independent-a", identify(a2))
    step("independent-a-scores", f"JSON.stringify({service}.segments)")
    check("low-score-held-out-rejected", f"{service}.segments.length > 0 && {service}.segments.every(row => row.state === 'unknown')")
    queued("known-a", identify(a1))
    check("a-match", f"{service}.segments.some(row => row.speaker === '示例甲' && row.state === 'matched')")
    queued("unknown-b", identify(b, True))
    check("reject-unregistered", f"{service}.segments.length > 0 && {service}.segments.every(row => row.state === 'unknown' && !row.transcript)")
    queued("append-a", enroll(a_en, "", "", f"{service}.profiles[0].id"))
    check("multi-enrollment", f"{service}.profiles[0].samples === 2")
    queued("enroll-b", enroll(b, "示例乙", "zh"))
    check("enrollment-b", f"{service}.profiles.length === 2")
    queued("two-speaker-handoff", identify(joined, True))
    step("handoff-results", f"JSON.stringify({service}.segments)")
    step("handoff-timing", f"JSON.stringify({service}.metrics)")
    check("handoff-ordered", f"{service}.segments.some(row => row.speaker === '示例甲') && "
          f"{service}.segments.some(row => row.speaker === '示例乙') && "
          f"{service}.segments.every((row,index,rows) => !index || row.start_ms >= rows[index-1].end_ms)")
    check("waveform-available", f"{service}.waveform.length === 640 && {service}.segments.some(row => row.state === 'unknown')")
    steps.append({"name":"timeline-click", "expression":"true", "tapObject":"speakerTimeline"})
    check("locked-chinese-transcript", f"{service}.segments.some(row => row.speaker === '示例甲' && row.language === 'zh' && /[\\u4e00-\\u9fff]/.test(row.transcript))")
    check("export-result", f"{service}.exportResult({literal(out.as_uri())})")
    check("edit-profile", f"{service}.updateProfile({service}.profiles[0].id,'示例甲','en')")
    queued("english-transcript", identify(a_en, True))
    step("english-result", f"JSON.stringify({service}.segments)")
    check("locked-english-transcript", f"{service}.segments.some(row => row.speaker === '示例甲' && row.language === 'en' && /[a-z]{{3}}/i.test(row.transcript))")
    check("remove-profile", f"{service}.removeProfile({service}.profiles[1].id) && {service}.profiles.length === 1")

    (directory / "steps.json").write_text(json.dumps(steps, ensure_ascii=False), encoding="utf-8")
    (directory / "reopen.json").write_text(json.dumps([{
        "name": "voiceprint-library-persistence",
        "expression": f"if (!App.session.loggedIn || {service}.profiles.length !== 1 || "
                      f"{service}.profiles[0].samples !== 2 || {service}.profiles[0].language !== 'en') "
                      "throw new Error('Voiceprint persistence'); App.nav.push('analysis',{analysis_tab:5}); true",
        "until": f"{service}.ready",
    }], ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("samples", type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    build_steps(args.work.resolve(), args.samples.resolve())
