#!/bin/sh
set -eu
# Separate runtime: do not modify the model or speech synthesis environments.
runtime=/userdata/ai-workbench/analytics-venv
if ! pkg-config --exists alsa fftw3; then
    sudo -n apt-get install -y libasound2-dev libfftw3-dev
fi
python3 -m venv --without-pip --system-site-packages "$runtime"
if ! "$runtime/bin/python" -m pip --version >/dev/null 2>&1; then
    bootstrap=$(mktemp /tmp/fieldops-analytics-pip.XXXXXX.py)
    trap 'rm -f "$bootstrap"' EXIT
    curl -fL --max-time 60 https://bootstrap.pypa.io/get-pip.py -o "$bootstrap"
    "$runtime/bin/python" "$bootstrap"
fi
"$runtime/bin/python" -m pip install numpy==2.2.6 scipy==1.14.1 spafe==0.3.3 pypdf==6.21.3
