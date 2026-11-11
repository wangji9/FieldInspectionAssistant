#!/bin/bash
set -euo pipefail
APP=/userdata/ai-workbench
mkdir -p "$APP/tts"
if [ ! -x "$APP/tts-venv/bin/python" ]; then
  python3 -m venv --without-pip "$APP/tts-venv"
fi
if [ ! -s "$APP/tts-pip.pyz" ]; then
  curl -fL --retry 2 https://bootstrap.pypa.io/pip/pip.pyz -o "$APP/tts-pip.pyz"
fi
"$APP/tts-venv/bin/python" "$APP/tts-pip.pyz" install sherpa-onnx==1.12.40 numpy==2.5.4
if [ ! -s "$APP/tts/vits-icefall-zh-aishell3/model.onnx" ]; then
  curl -fL --retry 2 --max-time 300 \
    https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-icefall-zh-aishell3.tar.bz2 \
    -o "$APP/tts/aishell3.tar.bz2"
  tar -xjf "$APP/tts/aishell3.tar.bz2" -C "$APP/tts"
fi
echo 'Offline Chinese speech environment ready.'
