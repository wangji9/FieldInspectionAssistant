#!/usr/bin/env bash
set -euo pipefail

runtime="${FIELDOPS_SPEAKER_HOME:-/userdata/fieldops-speaker}"
cache="${1:-${XDG_CACHE_HOME:-$HOME/.cache}/fieldops-speaker}"
[[ "$(uname -m)" == aarch64 && "$runtime" == /* && "$runtime" != / ]] || {
    printf '%s\n' 'An ARM64 board and an absolute runtime directory are required.' >&2
    exit 2
}
mkdir -p -- "$cache" "$(dirname -- "$runtime")"
release='sherpa-onnx-v2.0.1-linux-aarch64-shared-cpu.tar.bz2'
speaker='3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx'
whisper='sherpa-onnx-whisper-tiny.tar.bz2'
diarization='pyannote-segmentation-3.0.int8.onnx'

fetch() {
    local name="$1" digest="$2" url="$3"
    if [[ ! -f "$cache/$name" ]]; then
        curl --fail --location --retry 2 "$url" --output "$cache/$name.download"
        printf '%s  %s\n' "$digest" "$cache/$name.download" | sha256sum --check --status
        mv -- "$cache/$name.download" "$cache/$name"
    fi
    printf '%s  %s\n' "$digest" "$cache/$name" | sha256sum --check
}

fetch "$release" a8fc9fb3940d1aa3f83136767742b05068f00253f560d53c4f5aeb75ab4cb350 \
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.1/$release"
fetch "$speaker" f682b514c05d947ee3fa91cd6ec6c5c7543479a128373fa29b1faedccd21fd11 \
    "https://huggingface.co/csukuangfj/speaker-embedding-models/resolve/main/$speaker"
fetch "$whisper" c46116994e539aa165266d96b325252728429c12535eb9d8b6a2b10f129e66b1 \
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$whisper"
fetch "$diarization" d582f4b4c6b48205de7e0643c57df0df5615a3c176189be3fc461e9d18827b5d \
    "https://huggingface.co/csukuangfj/sherpa-onnx-pyannote-segmentation-3-0/resolve/main/model.int8.onnx"

stage="$(mktemp -d "$(dirname -- "$runtime")/.speaker-install.XXXXXXXX")"
trap 'rm -rf -- "$stage"' EXIT
tar --no-same-owner --no-same-permissions -xjf "$cache/$release" -C "$stage" --strip-components=1
install -d "$stage/models" "$stage/whisper"
install -m 0644 "$cache/$speaker" "$stage/models/$speaker"
install -m 0644 "$cache/$diarization" "$stage/models/$diarization"
tar --no-same-owner --no-same-permissions -xjf "$cache/$whisper" -C "$stage/whisper" --strip-components=1 \
    sherpa-onnx-whisper-tiny/tiny-encoder.int8.onnx \
    sherpa-onnx-whisper-tiny/tiny-decoder.int8.onnx \
    sherpa-onnx-whisper-tiny/tiny-tokens.txt
test -f "$stage/lib/libsherpa-onnx-c-api.so"
test -x "$stage/bin/sherpa-onnx-offline"
chmod 0755 "$stage"
if [[ -e "$runtime" ]]; then
    backup="${runtime}.previous-$(date +%Y%m%d-%H%M%S)"
    mv -- "$runtime" "$backup"
    if ! mv -- "$stage" "$runtime"; then
        mv -- "$backup" "$runtime"
        exit 1
    fi
else
    mv -- "$stage" "$runtime"
fi
printf 'Installed sherpa-onnx and CAM++ under %s\n' "$runtime"
