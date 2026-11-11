#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf '%s\n' 'Usage: build_stream_runner.sh <yolov5|yolov6|yolov8|yolov8_seg|yolov8_pose|yolo26|yolo26_segment|yolo26_pose> <output directory>' >&2
    exit 2
fi

case "$1" in
    yolov5) definition=FIELDOPS_YOLOV5 ;;
    yolov6) definition=FIELDOPS_YOLOV6 ;;
    yolov8) definition=FIELDOPS_YOLOV8 ;;
    yolov8_seg) definition=FIELDOPS_YOLOV8_SEG ;;
    yolo26) definition=FIELDOPS_YOLO26 ;;
    yolo26_segment) definition=FIELDOPS_YOLO26_SEGMENT ;;
    yolo26_pose) definition=FIELDOPS_YOLO26_POSE ;;
    yolov8_pose) definition=FIELDOPS_YOLOV8_POSE ;;
    *) exit 2 ;;
esac

sdk=${FIELDOPS_RKNN3_ZOO:-/userdata/ai-workbench/vendor/rknn3-model-zoo}
flags="-D$definition -DFIELDOPS_FAST_LETTERBOX"
link_flags="-Wl,--wrap=convert_image_with_letterbox"
if [ "${FIELDOPS_VISION_BACKEND:-rknn3}" = rknn2 ]; then
    sdk=${FIELDOPS_RKNN2_ZOO:?Set FIELDOPS_RKNN2_ZOO to the RKNN Model Zoo checkout}
    flags="$flags -DFIELDOPS_RKNN2"
fi
script_dir=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/fieldops-stream-runner.XXXXXX")
trap 'rm -r -- "$work"' EXIT
example="$work/examples/$1"
mkdir -p "$example"
ln -s "$(realpath "$sdk/3rdparty")" "$work/3rdparty"
ln -s "$(realpath "$sdk/utils")" "$work/utils"
cp -a "$sdk/examples/$1/cpp" "$example/cpp"
cp "$script_dir/stream_runner.cpp" "$example/cpp/main.cc"

if [ "$1" = yolo26 ]; then
    # Traverse contiguous class planes for the INT8 detector's per-cell argmax.
    python3 - "$example/cpp/postprocess.cc" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
source = path.read_text()
start = source.index("static int process_i8(")
end = source.index("static int process_fp32(", start)
decoder = source[start:end]
marker = "    int8_t score_thres_i8 = qnt_f32_to_affine(score_cmp_threshold, score_zp, score_scale);\n"
assert decoder.count(marker) == 1
decoder = decoder.replace(marker, marker + """
    static thread_local std::vector<int8_t> best_scores;
    static thread_local std::vector<int8_t> best_classes;
    best_scores.assign(grid_len, -128);
    best_classes.assign(grid_len, -1);
    for (int c = 0; c < OBJ_CLASS_NUM; ++c) {
        const int8_t *plane = score_tensor + c * grid_len;
        int cell = 0;
#if defined(__aarch64__)
        const auto category = vdupq_n_s8(c);
        for (; cell + 15 < grid_len; cell += 16) {
            const auto score = vld1q_s8(plane + cell);
            const auto best = vld1q_s8(best_scores.data() + cell);
            const auto better = vcgtq_s8(score, best);
            vst1q_s8(best_scores.data() + cell, vmaxq_s8(score, best));
            auto *classes = best_classes.data() + cell;
            vst1q_s8(classes, vbslq_s8(better, category, vld1q_s8(classes)));
        }
#endif
        for (; cell < grid_len; ++cell) {
            const int8_t score = plane[cell];
            if (score > best_scores[cell]) {
                best_scores[cell] = score;
                best_classes[cell] = c;
            }
        }
    }
""")
declarations = "            int max_class_id = -1;\n            int8_t max_score_i8 = -128;\n"
assert decoder.count(declarations) == 1
decoder = decoder.replace(declarations, """            const int max_class_id = best_classes[offset];
            const int8_t max_score_i8 = best_scores[offset];
""")
first = decoder.index("            for (int c= 0; c< OBJ_CLASS_NUM; c++){")
last = decoder.index("            // compute box", first)
decoder = decoder[:first] + decoder[last:]
decoder = decoder.replace("if (max_class_id >= 0){", "if (max_class_id >= 0 && max_score_i8 >= score_thres_i8){")
path.write_text("#if defined(__aarch64__)\n#include <arm_neon.h>\n#endif\n" + source[:start] + decoder + source[end:])
PY
fi

if [ "$1" = yolo26_segment ]; then
    python3 "$script_dir/optimize_yolo26_masks.py" "$example/cpp/postprocess.cc"
fi

cmake -S "$example/cpp" -B "$work/build" -DTARGET_SOC=rk3588 \
    -DCMAKE_BUILD_TYPE=Release -DDISABLE_RGA=ON "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=$link_flags"
cmake --build "$work/build" --target "rknn_${1}_demo" --parallel 4
mkdir -p "$2/model"
install -m 755 "$work/build/rknn_${1}_demo" "$2/stream-runner.new"
mv -f "$2/stream-runner.new" "$2/stream-runner"
if [ "$1" = yolov8_pose ]; then
    cp "$sdk/examples/$1/model/yolov8_pose_labels_list.txt" "$2/model/"
    cp "$sdk/examples/$1/model/yolov8_pose_labels_list.txt" "$2/model/labels.txt"
else
    cp "$sdk/examples/$1/model/coco_80_labels_list.txt" "$2/model/"
fi
sample="$sdk/examples/$1/model/bus.jpg"
if [ "$1" = yolo26_pose ]; then
    printf '%s\n' person > "$2/model/labels.txt"
    sample="$sdk/examples/$1/model/little_boy.jpg"
fi
if [ -f "$sample" ]; then
    cp "$sample" "$2/model/sample.jpg"
fi
