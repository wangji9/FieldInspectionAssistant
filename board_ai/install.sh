#!/bin/bash
set -euo pipefail
APP=/userdata/ai-workbench
QTAPP=/home/ztl/FieldInspectionAssistant
sudo -n apt-get update
sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y \
  cmake g++ git curl pkg-config hunspell-en-gb alsa-utils pulseaudio-utils python3-flask python3-numpy python3-pil python3-serial ffmpeg \
  qt6-base-dev qt6-declarative-dev libqt6sql6-sqlite \
  qml6-module-qtquick qml6-module-qtquick-layouts qml6-module-qtquick-window \
  qml6-module-qtquick-controls qml6-module-qtquick-templates \
  qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript \
  qml6-module-qtquick-virtualkeyboard qt6-virtualkeyboard-plugin qt6-virtualkeyboard-dev \
  qml6-module-qt-labs-folderlistmodel \
  libopencv-dev libomp-dev libsndfile1-dev libfftw3-dev fonts-noto-cjk fonts-noto-core fonts-inter fonts-dejavu-core
mkdir -p "$APP/vendor" "$APP/data"
bash "$APP/install_speech.sh"
if [ ! -d "$APP/vendor/rknn-llm/.git" ]; then
  git clone https://github.com/airockchip/rknn-llm.git "$APP/vendor/rknn-llm"
  git -C "$APP/vendor/rknn-llm" checkout f7390530443bf84f0394255a449d7cbe81e69d1c
fi
if [ ! -d "$APP/vendor/rknn3-model-zoo/.git" ]; then
  git clone https://github.com/airockchip/rknn3-model-zoo.git "$APP/vendor/rknn3-model-zoo"
  git -C "$APP/vendor/rknn3-model-zoo" checkout ca92e7fe1a418b220985455446a986b80d6a899f
fi
python3 "$APP/patch_asr_metrics.py"
python3 "$APP/patch_rk1828_demos.py"
python3 "$APP/patch_resident_demos.py"
(cd "$APP/vendor/rknn3-model-zoo"; bash build-linux.sh -t rk3588 -a aarch64 -d Qwen3_ASR -b Release)
(cd "$APP/vendor/rknn3-model-zoo"; bash build-linux.sh -t rk3588 -a aarch64 -d Qwen3_5 -b Release)
(cd "$APP/vendor/rknn3-model-zoo"; bash build-linux.sh -t rk3588 -a aarch64 -d Qwen2_5_VL -b Release)
cmake -S "$APP" -B "$APP/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$APP/build" -j4
cmake -S "$QTAPP" -B "$QTAPP/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$QTAPP/build" -j4
chmod +x "$QTAPP"/deploy/*.sh
sudo -n cp "$APP/board-ai.service" /etc/systemd/system/board-ai.service
sudo -n systemctl daemon-reload
sudo -n systemctl enable --now board-ai
chmod +x "$QTAPP/deploy/open_field_inspection.py" "$QTAPP/deploy/install_desktop_launcher.py"
if [ "$(id -u)" = 0 ]; then
  sudo -u ztl /usr/bin/python3 "$QTAPP/deploy/install_desktop_launcher.py"
else
  /usr/bin/python3 "$QTAPP/deploy/install_desktop_launcher.py"
fi
echo 'Installed. Launch /home/ztl/FieldInspectionAssistant/deploy/run_field_inspection.sh'
