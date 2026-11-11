#include "qwen3_vl.h"
#include "image_utils.h"
#include "../resident_protocol.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

const char *system_prompt = "<|im_start|>system\n你是工业巡检视频观察员。只报告画面证据。<|im_end|>\n";
const char *prompt_prefix = "<|im_start|>user\n";
const char *prompt_postfix = "<|im_end|>\n<|im_start|>assistant\n";

namespace {
constexpr size_t FrameCount = 3;
constexpr int InputSize = 384;

struct Callbacks {
    Tokenizer tokenizer;
    QFile embeddingFile;
    const uchar *embedding = nullptr;
    size_t rowSize = 0;
    int vocabSize = 0;
    std::vector<int32_t> output;
    bool failed = false;

    Callbacks(const char *tokenizerPath, const char *embeddingPath)
        : tokenizer(TOKENIZER_BACKEND_LLAMA, tokenizerPath), embeddingFile(embeddingPath) {}
};

int tokenize(void *data, const char *text, int32_t length, int32_t *ids, int32_t capacity)
{
    return static_cast<Callbacks *>(data)->tokenizer.Tokenize(text, length, ids, capacity);
}

int embed(void *data, int32_t *ids, uint64_t count, void *output, uint64_t bytes)
{
    const auto &cb = *static_cast<Callbacks *>(data);
    if (count * cb.rowSize != bytes)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] < 0 || ids[i] >= cb.vocabSize)
            return -1;
        std::memcpy(static_cast<uchar *>(output) + i * cb.rowSize,
                    cb.embedding + size_t(ids[i]) * cb.rowSize, cb.rowSize);
    }
    return 0;
}

int receive(void *data, RKLLMResult *result, LLMCallState state)
{
    auto &cb = *static_cast<Callbacks *>(data);
    if (state == RKLLM_RUN_ERROR)
        cb.failed = true;
    if (state == RKLLM_RUN_NORMAL && result && result->num_tokens > 0)
        cb.output.insert(cb.output.end(), result->token_ids, result->token_ids + result->num_tokens);
    return 0;
}

int runClip(rknn_app_context_t &context, Callbacks &cb, const QJsonObject &request)
{
    const auto files = request["frames"].toArray();
    const auto offsets = request["offsets"].toArray();
    if (files.size() != FrameCount || offsets.size() != FrameCount)
        return -1;

    auto &vision = context.vision;
    auto &llm = context.llm;
    std::array<size_t, 4> featureBytes{};
    std::array<std::vector<float16>, 4> features;
    for (size_t layer = 0; layer < features.size(); ++layer) {
        const auto *attr = vision.outputs[layer].attr;
        size_t elements = 1;
        for (size_t axis = 0; axis < attr->n_dims; ++axis)
            elements *= attr->shape[axis];
        featureBytes[layer] = elements * sizeof(float16);
        const auto bytes = vision.outputs[layer].mem->size;
        if (!elements || bytes < featureBytes[layer] || bytes % sizeof(float16))
            return -1;
        // The encoder writes its aligned output. Each following frame replaces
        // the previous frame's trailing padding, keeping the token rows contiguous.
        features[layer].resize((FrameCount - 1) * elements + bytes / sizeof(float16));
    }

    QElapsedTimer clock;
    clock.start();
    QByteArray prompt;
    for (size_t i = 0; i < FrameCount; ++i) {
        if (fieldops_cancelled.load())
            return 0;
        image_buffer_t image{};
        const auto file = QFile::encodeName(files[int(i)].toString());
        const auto read = read_image(file.constData(), &image);
        std::unique_ptr<void, decltype(&free)> pixels(image.virt_addr, free);
        if (read != 0)
            return -1;
        const auto target = [&](size_t layer) {
            return features[layer].data() + i * featureBytes[layer] / sizeof(float16);
        };
        const int ret = inference_qwen3_vl_vision(&vision, &image, target(0), target(1), target(2), target(3));
        if (ret != 0)
            return ret;
        prompt += QByteArray("帧") + QByteArray::number(i + 1) + "（" +
                  QByteArray::number(offsets[int(i)].toInt()) + "毫秒）<image>\n";
    }
    const auto visionMs = clock.elapsed();
    prompt += "对比三帧，判断人员、工具或设备是否有明确可见的动作或状态变化。"
              "只输出一行。确认有变化才写帧号，例如 1-3|人员从桌前站起。"
              "画面稳定、只是坐着或无法确认时必须用0，例如 0|人员坐在桌前。"
              "只描述看得清的内容，勿推测小物品或设备内部状态。";

    rknn3_llm_input inputs[4]{};
    inputs[0].input_type = RKNN3_LLM_INPUT_MULTIMODAL;
    auto &input = inputs[0].multimodal_input;
    input.name = "input_embeds";
    input.prompt = prompt.constData();
    input.image.image_embed = features[0].data();
    input.image.n_image = FrameCount;
    input.image.n_image_tokens = vision.embeds_shape[vision.embeds_ndims - 2];
    input.image.image_width = vision.model_width;
    input.image.image_height = vision.model_height;
    input.image.image_start = "<|vision_start|>";
    input.image.image_end = "<|vision_end|>";
    input.image.image_content = "<|image_pad|>";

    std::array<rknn3_tensor_mem *, 3> extra{};
    ResidentCleanup freeExtra{[&] {
        for (auto *mem : extra)
            if (mem)
                rknn3_destroy_mem(llm.rknn_ctx, mem);
    }};
    for (size_t i = 0; i < extra.size(); ++i) {
        const auto layer = i + 1;
        const auto bytes = features[layer].size() * sizeof(float16);
        rknn3_tensor_attr attr{};
        attr.index = uint32_t(i + 2);
        if (rknn3_query(llm.rknn_ctx, RKNN3_QUERY_INPUT_ATTR, &attr, sizeof(attr)) != 0)
            return -1;
        extra[i] = rknn3_create_mem(llm.rknn_ctx, bytes, attr.core_id, RKNN3_FLAG_MEMORY_CACHEABLE);
        if (!extra[i] || !extra[i]->virt_addr || extra[i]->size < bytes)
            return -1;
        std::memcpy(extra[i]->virt_addr, features[layer].data(), bytes);
        inputs[layer].input_type = RKNN3_LLM_INPUT_AUX;
        inputs[layer].aux_input.mem = extra[i];
    }
    cb.output.clear();
    cb.failed = false;
    rknn3_llm_infer_param limits{};
    limits.max_new_tokens = 56;
    const int ret = rknn3_session_run(llm.rknn_sess, inputs, 4, &limits);
    if (ret != 0 || cb.failed || cb.output.empty())
        return ret ? ret : -1;
    const auto decoded = cb.tokenizer.Decode(cb.output.data(), cb.output.size());
    const auto result = QJsonDocument(QJsonObject{{"text", QString::fromStdString(decoded)},
        {"vision_ms", double(visionMs)}, {"inference_ms", double(clock.elapsed())}}).toJson(QJsonDocument::Compact);
    printf("\n@@VIDEO@@%s\n", result.constData());
    fflush(stdout);
    return 0;
}
}

int main(int argc, char **argv)
{
    if (argc != 8)
        return 2;
    QElapsedTimer clock;
    clock.start();
    Callbacks cb(argv[5], argv[6]);
    VocabInfo vocab{};
    cb.tokenizer.GetVocabInfo(&vocab);
    cb.vocabSize = vocab.vocab_size;
    if (!cb.embeddingFile.open(QIODevice::ReadOnly) || cb.vocabSize <= 0)
        return 2;
    const auto bytes = cb.embeddingFile.size();
    if (bytes <= 0 || bytes % cb.vocabSize)
        return 2;
    cb.rowSize = size_t(bytes / cb.vocabSize);
    cb.embedding = cb.embeddingFile.map(0, bytes);
    if (!cb.embedding)
        return 2;

    rknn3_llm_param params{};
    char logitsName[] = "logits";
    params.logits_name = logitsName;
    const QByteArray contextLength(argv[7]);
    if (contextLength != "66560" && contextLength != "34816")
        return 2;
    params.max_context_len = contextLength.toInt();
    params.sampling_param.top_k = 1;
    params.sampling_param.top_p = 1.0f;
    params.sampling_param.temperature = 1.0f;
    params.sampling_param.repeat_penalty = 1.0f;
    params.vocab_info.vocab_size = cb.vocabSize;
    params.vocab_info.n_special_eos_id = vocab.n_special_eos_id;
    params.vocab_info.n_special_bos_id = vocab.n_special_bos_id;
    params.vocab_info.linefeed_id = vocab.linefeed_id;
    std::memcpy(params.vocab_info.special_eos_id, vocab.special_eos_id, sizeof(params.vocab_info.special_eos_id));
    std::memcpy(params.vocab_info.special_bos_id, vocab.special_bos_id, sizeof(params.vocab_info.special_bos_id));

    RKLLMCallback callback{};
    callback.result_callback = receive;
    callback.result_userdata = &cb;
    callback.tokenizer_callback = tokenize;
    callback.tokenizer_userdata = &cb;
    callback.embed_callback = embed;
    callback.embed_userdata = &cb;
    rknn_app_context_t context{};
    context.model_width = context.model_height = InputSize;
    if (init_qwen3_vl_model(&context, argv[3], argv[4], argv[1], argv[2],
                            &params, 1, callback, 0xff, 0xff) != 0)
        return 1;
    ResidentCleanup cleanup{[&] { release_qwen3_vl_model(&context); }};
    if (context.vision.io_num.n_output != 4 || context.vision.embeds_ndims < 2)
        return 1;
    resident_loop(clock.elapsed(), context.llm.rknn_sess, context.llm.rknn_ctx,
                  [&](const QJsonObject &request) { return runClip(context, cb, request); });
    return 0;
}
