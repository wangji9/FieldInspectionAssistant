#include "rk1828_model.h"

#ifdef FIELDOPS_YOLOV8_POSE
#include "yolov8-pose.h"
#else
#include "yolov8_seg.h"
#endif
#include "image_utils.h"
#include <opencv2/imgproc.hpp>
#ifdef FIELDOPS_YOLOV8_SEG
#include <arm_neon.h>
#endif
#include <memory>

namespace {
#ifdef FIELDOPS_YOLOV8_POSE
constexpr std::initializer_list<unsigned> Sizes{65 * 80 * 80, 65 * 40 * 40, 65 * 20 * 20, 17 * 3 * 8400};
#else
constexpr std::initializer_list<unsigned> Sizes{
    64 * 80 * 80, 80 * 80 * 80, 80 * 80, 32 * 80 * 80,
    64 * 40 * 40, 80 * 40 * 40, 40 * 40, 32 * 40 * 40,
    64 * 20 * 20, 80 * 20 * 20, 20 * 20, 32 * 20 * 20, 32 * 160 * 160};
#endif
std::unique_ptr<Rk1828Model> model;
std::array<rknn_tensor_attr, Sizes.size()> decoderAttributes;
std::array<rknn_output, Sizes.size()> decoderBuffers;
cv::Mat input;
}

int init_rk1828_bridge(const char *path, const char *, rknn_app_context_t *context,
                      uint32_t, const char *)
{
    auto runner = std::make_unique<Rk1828Model>();
    if (!runner->load(path, Sizes) || runner->width() != 640 || runner->height() != 640)
        return -1;
    for (unsigned i = 0; i < Sizes.size(); ++i) {
        const auto &attribute = runner->outputAttr(i);
        if (attribute.n_dims != 4 || attribute.layout != RKNN3_TENSOR_NCHW)
            return -1;
        decoderAttributes[i].n_dims = attribute.n_dims;
        std::copy_n(attribute.shape, attribute.n_dims, decoderAttributes[i].dims);
        decoderBuffers[i].buf = const_cast<float *>(runner->output(i));
    }
    context->model_width = runner->width();
    context->model_height = runner->height();
    context->model_channel = 3;
    context->io_num.n_output = Sizes.size();
    context->output_attrs = decoderAttributes.data();
    context->is_quant = false;
    model = std::move(runner);
    cv::setNumThreads(1);
    return 0;
}

int run_rk1828_bridge(rknn_app_context_t *context, image_buffer_t *image,
                     object_detect_result_list *results)
{
    const cv::Mat source(image->height, image->width, CV_8UC3, image->virt_addr);
    letterbox_t geometry{};
    geometry.scale = std::min(float(context->model_width) / source.cols,
                              float(context->model_height) / source.rows);
    const int width = std::max(4, int(source.cols * geometry.scale) & ~3);
    const int height = std::max(2, int(source.rows * geometry.scale) & ~1);
    geometry.x_pad = ((context->model_width - width) / 2) & ~1;
    geometry.y_pad = ((context->model_height - height) / 2) & ~1;
    const cv::Rect content(geometry.x_pad, geometry.y_pad, width, height);
    input.create(context->model_height, context->model_width, CV_8UC3);
    input.setTo(cv::Scalar::all(114));
    cv::resize(source, input(content), content.size(), 0, 0, cv::INTER_LINEAR);
    if (!model->infer(input))
        return -1;
#ifdef FIELDOPS_YOLOV8_SEG
    context->input_image_width = source.cols;
    context->input_image_height = source.rows;
#endif
    return post_process(context, decoderBuffers.data(), &geometry, BOX_THRESH, NMS_THRESH, results);
}

int release_rk1828_bridge(rknn_app_context_t *)
{
    model.reset();
    return 0;
}

#ifdef FIELDOPS_YOLOV8_SEG
void matmul_by_cpu_uint8(std::vector<float> &coefficients, float *prototypes,
                         uint8_t *output, int instances, int channels, int pixels)
{
    const auto zero = vdupq_n_f32(0);
    const auto maskValue = vdupq_n_u8(4);
    for (int instance = 0; instance < instances; ++instance) {
        const auto *weights = coefficients.data() + instance * channels;
        auto *mask = output + instance * pixels;
        int x = 0;
        for (; x + 16 <= pixels; x += 16) {
            auto a = zero, b = zero, c = zero, d = zero;
            for (int k = 0; k < channels; ++k) {
                const auto *source = prototypes + k * pixels + x;
                a = vfmaq_n_f32(a, vld1q_f32(source), weights[k]);
                b = vfmaq_n_f32(b, vld1q_f32(source + 4), weights[k]);
                c = vfmaq_n_f32(c, vld1q_f32(source + 8), weights[k]);
                d = vfmaq_n_f32(d, vld1q_f32(source + 12), weights[k]);
            }
            const auto ab = vcombine_u16(vmovn_u32(vcgtq_f32(a, zero)), vmovn_u32(vcgtq_f32(b, zero)));
            const auto cd = vcombine_u16(vmovn_u32(vcgtq_f32(c, zero)), vmovn_u32(vcgtq_f32(d, zero)));
            vst1q_u8(mask + x, vandq_u8(vcombine_u8(vmovn_u16(ab), vmovn_u16(cd)), maskValue));
        }
        for (; x < pixels; ++x) {
            float score = 0;
            for (int k = 0; k < channels; ++k)
                score += weights[k] * prototypes[k * pixels + x];
            mask[x] = score > 0 ? 4 : 0;
        }
    }
}
#endif
