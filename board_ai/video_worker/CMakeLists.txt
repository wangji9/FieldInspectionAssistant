cmake_minimum_required(VERSION 3.16)
project(fieldops_video_worker LANGUAGES C CXX)

find_package(Qt6 REQUIRED COMPONENTS Core)
set(TARGET_SOC rk3588)
set(TARGET_LIB_ARCH aarch64)
set(DEMO "${RKNN3_ZOO}/examples/Qwen3_VL/cpp")
add_subdirectory("${RKNN3_ZOO}/3rdparty" third_party)
add_subdirectory("${RKNN3_ZOO}/utils" utils)

add_executable(video-worker video_worker.cpp
    "${DEMO}/qwen3_vl.cc" "${DEMO}/llm/rknn_qwen3_vl_llm.cc"
    "${DEMO}/vision/rknn_qwen3_vl_vision.cc")
target_compile_features(video-worker PRIVATE cxx_std_17)
target_include_directories(video-worker PRIVATE
    "${DEMO}" "${DEMO}/llm" "${DEMO}/vision" ${LIBRKNNAPI_INCLUDES} ${LIBTOKENIZER_INCLUDES})
target_link_libraries(video-worker PRIVATE Qt6::Core timeutils imageutils fileutils
    ${LIBRKNNAPI} ${LIBTOKENIZER} dl pthread)
