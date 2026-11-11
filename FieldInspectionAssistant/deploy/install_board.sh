#!/usr/bin/env bash
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
runtime=/userdata/ai-workbench
sdk="$runtime/vendor/rknn3-model-zoo"
jobs=${FIELDOPS_BUILD_JOBS:-4}

if [[ "$(id -un)" != ztl || "$project" != /home/ztl/FieldInspectionAssistant ]]; then
  printf '%s\n' 'Run as ztl from /home/ztl/FieldInspectionAssistant.' >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git curl pkg-config python3-venv \
  python3-flask python3-numpy python3-pil python3-serial \
  alsa-utils pulseaudio-utils ffmpeg hunspell-en-gb \
  qt6-base-dev qt6-declarative-dev qt6-virtualkeyboard-dev qt6-virtualkeyboard-plugin \
  libqt6sql6-sqlite qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-dialogs qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtquick-virtualkeyboard \
  qml6-module-qtqml-workerscript qml6-module-qt-labs-folderlistmodel \
  libasound2-dev libfftw3-dev libxcb1-dev libopencv-dev libomp-dev libsndfile1-dev \
  desktop-file-utils wmctrl fonts-noto-cjk fonts-noto-core fonts-inter fonts-dejavu-core

for path in "$runtime" /userdata/models /opt/rk1828-models; do
  if [[ ! -d "$path" ]]; then
    sudo install -d -o "$(id -un)" -g "$(id -gn)" "$path"
  fi
done
mkdir -p "$runtime/vendor" "$runtime/data" "$runtime/build" "$runtime/video_worker/build"

checkout_sdk() {
  local remote=$1 commit=$2 directory=$3
  if [[ ! -d "$directory/.git" ]]; then
    git clone "$remote" "$directory"
    git -C "$directory" checkout --detach "$commit"
  elif [[ "$(git -C "$directory" rev-parse HEAD)" != "$commit" ]]; then
    printf 'Unexpected SDK revision in %s. Expected %s.\n' "$directory" "$commit" >&2
    exit 1
  fi
}

checkout_sdk https://github.com/airockchip/rknn-llm.git \
  f7390530443bf84f0394255a449d7cbe81e69d1c "$runtime/vendor/rknn-llm"
checkout_sdk https://github.com/airockchip/rknn3-model-zoo.git \
  ca92e7fe1a418b220985455446a986b80d6a899f "$sdk"

bash "$project/deploy/inference/install_speech.sh"
bash "$project/deploy/install_analytics.sh"
python3 "$project/deploy/inference/patch_asr_metrics.py"
python3 "$project/deploy/inference/patch_rk1828_demos.py"
python3 "$project/deploy/inference/patch_resident_demos.py"
for demo in Qwen3_ASR Qwen3_5 Qwen2_5_VL; do
  (cd "$sdk" && bash build-linux.sh -t rk3588 -a aarch64 -d "$demo" -b Release)
done

inference="$project/services/inference"
native_build="$runtime/build/fieldops-native"
video_build="$runtime/build/fieldops-video"
cmake -S "$inference" -B "$native_build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$native_build" --parallel "$jobs"
cmake -S "$inference/video_worker" -B "$video_build" \
  -DCMAKE_BUILD_TYPE=Release -DRKNN3_ZOO="$sdk"
cmake --build "$video_build" --parallel "$jobs"

cmake -S "$project" -B "$project/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project/build" --parallel "$jobs"

# Retain the deployed paths used by the Qt client and existing board services.
replace_runtime_file() {
  local source=$1 target=$2 mode=$3
  local staged
  staged=$(mktemp "$target.XXXXXX")
  if ! { install -m "$mode" "$source" "$staged" && mv -f -- "$staged" "$target"; }; then
    rm -f -- "$staged"
    return 1
  fi
}
for module in answer_policy asr_stream perf_profile resident_models server speech_audio speech_endpoint speech_session; do
  replace_runtime_file "$inference/$module.py" "$runtime/$module.py" 0644
done
replace_runtime_file "$project/services/audio/six_mic_control.py" "$runtime/six_mic_control.py" 0644
replace_runtime_file "$native_build/native_runner" "$runtime/build/native_runner" 0755
replace_runtime_file "$video_build/video-worker" "$runtime/video_worker/build/video-worker" 0755

sudo install -m 0644 "$project/deploy/systemd/board-ai.service" /etc/systemd/system/board-ai.service
sudo systemctl daemon-reload
sudo systemctl enable board-ai.service
sudo systemctl restart board-ai.service

install -D -m 0644 "$project/deploy/systemd/miiep-inference.service" \
  "$HOME/.config/systemd/user/miiep-inference.service"
systemctl --user daemon-reload
systemctl --user enable miiep-inference.service
systemctl --user restart miiep-inference.service

printf '%s\n' 'Run python3 deploy/install_desktop_launcher.py from the ztl Xfce desktop session to install the launcher.'
