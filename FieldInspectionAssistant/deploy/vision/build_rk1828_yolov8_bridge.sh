#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    printf '%s\n' 'Usage: build_rk1828_yolov8_bridge.sh <pose|segment> <RKNN3 Zoo> <RKNN2 Zoo> <example cpp directory> <output>' >&2
    exit 2
fi

case "$1" in
    pose) definition=FIELDOPS_YOLOV8_POSE; decoder="$4/postprocess.cc" ;;
    segment)
        definition=FIELDOPS_YOLOV8_SEG
        decoder=$(mktemp --suffix=.cc)
        trap 'rm -f -- "$decoder"' EXIT
        # Replace the vendor scalar mask product with the NEON implementation.
        awk '
            /^void matmul_by_cpu_uint8\(/ {
                print "void matmul_by_cpu_uint8(std::vector<float>&, float*, uint8_t*, int, int, int);"
                skip = 1; found = 1; next
            }
            skip && /^}/ { skip = 0; next }
            !skip { print }
            END { if (!found || skip) exit 1 }
        ' "$4/rknpu2/postprocess.cc" > "$decoder"
        ;;
    *) exit 2 ;;
esac

rk3=$2/3rdparty/rknpu3
rk2=$3/3rdparty
script_dir=$(cd "$(dirname "$0")" && pwd)

# Compile only the SDK's host decoder. Garbage collection discards its
# unused RKNN2 matmul helper, so the executable links to RKNN3 alone.
"${CXX:-g++}" -std=c++17 -O3 -march=armv8.2-a+fp16 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -D"$definition" -DFIELDOPS_RKNN3_BRIDGE -I"$4" -I"$3/utils" -I"$rk3/include" \
    -I"$rk2/rknpu2/include" -I"$rk2/librga/include" -I"$rk2/allocator/dma" -I"$rk2/allocator/drm" -I"$rk2/timer" \
    $(pkg-config --cflags opencv4) "$script_dir/stream_runner.cpp" "$script_dir/rk1828_yolov8_bridge.cpp" \
    "$decoder" "$rk3/Linux/aarch64/librknn3_api.so" -lopencv_imgproc -lopencv_core -o "$5"
