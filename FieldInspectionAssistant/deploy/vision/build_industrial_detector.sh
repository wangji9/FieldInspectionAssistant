#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    printf '%s\n' 'Usage: build_industrial_detector.sh <RKNN3 Model Zoo checkout> <output executable>' >&2
    exit 2
fi

sdk=$1/3rdparty/rknpu3
script_dir=$(cd "$(dirname "$0")" && pwd)
"${CXX:-g++}" -std=c++17 -O3 -march=armv8.2-a+fp16 -Wall -Wextra -I"$sdk/include" $(pkg-config --cflags opencv4) \
    "$script_dir/industrial_detector.cpp" "$sdk/Linux/aarch64/librknn3_api.so" -lopencv_imgproc -lopencv_core -o "$2"
