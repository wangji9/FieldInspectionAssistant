"""Specialize the RKNN3 YOLO26 INT8 segmentation decoder for the RK3588 host."""

from pathlib import Path
import sys


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError("Unexpected RKNN3 YOLO26 segmentation decoder")
    return source.replace(before, after)


path = Path(sys.argv[1])
source = path.read_text()
source = """#if defined(__aarch64__)
#include <arm_neon.h>
#endif
""" + source

# The SDK computes the same 32-term integer dot product for every prototype
# pixel. Accumulate eight adjacent pixels in parallel when the tensor's
# width stride permits it. The scalar tail also handles strided tensors.
source = replace_once(source, """        int proto_y_base = py * proto_h_stride;
        for (int px = proto_left; px <= proto_right; px++)
""", """        int proto_y_base = py * proto_h_stride;
        int px = proto_left;
#if defined(__aarch64__)
        if (proto_w_stride == 1)
        {
            const int16x8_t zero_point = vdupq_n_s16(proto_zp);
            const int32x4_t threshold = vdupq_n_s32(acc_threshold);
            for (; px + 7 <= proto_right; px += 8)
            {
                int32x4_t low = vdupq_n_s32(0);
                int32x4_t high = vdupq_n_s32(0);
                for (int c = 0; c < OBJ_MASK_DIM; ++c)
                {
                    const auto *plane = proto_data + proto_c_offsets[c] + proto_y_base;
                    const auto samples = vsubq_s16(vmovl_s8(vld1_s8(plane + px)), zero_point);
                    low = vmlal_n_s16(low, vget_low_s16(samples), coeff_delta[c]);
                    high = vmlal_high_n_s16(high, samples, coeff_delta[c]);
                }
                const auto bits = vcombine_u16(vmovn_u32(vcgtq_s32(low, threshold)),
                                               vmovn_u32(vcgtq_s32(high, threshold)));
                vst1_u8(proto_mask.data() + py * proto_w + px, vmovn_u16(bits));
            }
        }
#endif
        for (; px <= proto_right; ++px)
""")

# Read adjacent class scores together. Preserve the original tie-breaking
# order and quantized threshold, including the scalar fallback for a
# non-contiguous width stride.
start = source.index("static int process_i8(")
end = source.index("static int process_fp32(", start)
decoder = source[start:end]
decoder = replace_once(decoder, """    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int max_class_id = -1;
            int8_t max_score_i8 = -128;
            int score_base = i * score_h_stride + j * score_w_stride;
            for (int c= 0; c< class_count; c++){
                int score_offset = score_base + c * score_c_stride;
                int8_t score_i8 = score_tensor[score_offset];
                if((score_i8 >= score_thres_i8) && (score_i8 > max_score_i8))
                {
                    max_score_i8 = score_i8;
                    max_class_id = c;
                }
            }

            if (max_class_id >= 0){
""", """    static thread_local std::vector<int8_t> scores;
    static thread_local std::vector<int8_t> classes;
    scores.assign(grid_h * grid_w, -128);
    classes.assign(grid_h * grid_w, -1);
    for (int c = 0; c < class_count; ++c)
    {
        for (int i = 0; i < grid_h; ++i)
        {
            const auto *row = score_tensor + c * score_c_stride + i * score_h_stride;
            auto *best_scores = scores.data() + i * grid_w;
            auto *best_classes = classes.data() + i * grid_w;
            int j = 0;
#if defined(__aarch64__)
            if (score_w_stride == 1)
            {
                const auto category = vdupq_n_s8(c);
                for (; j + 15 < grid_w; j += 16)
                {
                    const auto score = vld1q_s8(row + j);
                    const auto best = vld1q_s8(best_scores + j);
                    const auto better = vcgtq_s8(score, best);
                    vst1q_s8(best_scores + j, vmaxq_s8(score, best));
                    vst1q_s8(best_classes + j, vbslq_s8(better, category, vld1q_s8(best_classes + j)));
                }
            }
#endif
            for (; j < grid_w; ++j)
            {
                const auto score = row[j * score_w_stride];
                if (score > best_scores[j])
                {
                    best_scores[j] = score;
                    best_classes[j] = c;
                }
            }
        }
    }
    for (int i = 0; i < grid_h; ++i)
    {
        for (int j = 0; j < grid_w; ++j)
        {
            const int cell = i * grid_w + j;
            const int max_class_id = classes[cell];
            const int8_t max_score_i8 = scores[cell];
            if (max_class_id >= 0 && max_score_i8 >= score_thres_i8){
""")
path.write_text(source[:start] + decoder + source[end:])
