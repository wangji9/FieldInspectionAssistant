#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf '%s\n' 'Usage: build_keypoint_runner.sh <RKNN3 Model Zoo checkout> <output executable>' >&2
    exit 2
fi

sdk=$1/3rdparty/rknpu3
script_dir=$(cd "$(dirname "$0")" && pwd)
lib="$sdk/Linux/aarch64/librknn3_api.so"
test -n "$lib"
"${CXX:-g++}" -std=c++17 -O3 -march=armv8.2-a+fp16 -Wall -Wextra -I"$sdk/include" $(pkg-config --cflags opencv4) \
    "$script_dir/keypoint_runner.cpp" "$lib" -lopencv_calib3d -lopencv_imgproc -lopencv_core \
    -o "$2"
