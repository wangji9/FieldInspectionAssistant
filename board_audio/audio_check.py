#!/usr/bin/env python3
"""Record a finite sample or play the explicit USB output; report sample levels."""
import argparse
import array
import json
import math
from pathlib import Path
import subprocess
import wave


def main():
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['record', 'play', 'levels'])
    p.add_argument('file')
    p.add_argument('--seconds', type=int, default=10)
    args = p.parse_args()
    if args.action == 'record':
        if not 1 <= args.seconds <= 120:
            p.error('--seconds must be 1..120')
        subprocess.run(['arecord', '-q', '-D', 'pulse', '-f', 'S16_LE',
                        '-r', '16000', '-c', '1', '-d', str(args.seconds),
                        args.file], check=True, timeout=args.seconds + 15)
    if args.action == 'play':
        sinks = json.loads(subprocess.check_output(
            ['pactl', '-f', 'json', 'list', 'sinks'], text=True))
        sink = next(s['name'] for s in sinks if
                    'usb-C-Media_Electronics_Inc._USB_Audio_Device-' in s['name'])
        subprocess.run(['paplay', '--device=' + sink, args.file], check=True)
        return
    with wave.open(args.file) as w:
        if w.getsampwidth() != 2:
            raise ValueError('Expected 16-bit PCM WAV')
        a = array.array('h', w.readframes(w.getnframes()))
        rms = math.sqrt(sum(x*x for x in a) / max(len(a), 1))
        report = dict(file=str(Path(args.file).resolve()), rate=w.getframerate(),
                      channels=w.getnchannels(), duration_s=w.getnframes()/w.getframerate(),
                      peak=max(map(abs, a), default=0), rms=rms,
                      rms_dbfs=20*math.log10(rms/32768) if rms else None)
        print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
