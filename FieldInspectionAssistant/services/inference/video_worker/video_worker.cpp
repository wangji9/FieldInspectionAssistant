#include "qwen3_vl.h"
#include "image_utils.h"
#include "vision_input.h"
#include "../resident_protocol.h"

#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <array>
#include <algorithm>
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

class ClipBuffers {
public:
    explicit ClipBuffers(rknn_app_context_t &context) : context_(context) {}
    ~ClipBuffers()
    {
        for (auto *mem : aux_)
            if (mem)
                rknn3_destroy_mem(context_.llm.rknn_ctx, mem);
    }

    bool initialize()
    {
        auto &vision = context_.vision;
        if (canPackDirectly(vision))
            rgb_.resize(size_t(vision.model_width) * vision.model_height * fieldops::VisionChannels);
        for (size_t layer = 0; layer < logical_.size(); ++layer) {
            const auto *attr = vision.outputs[layer].attr;
            size_t elements = 1;
            for (size_t axis = 0; axis < attr->n_dims; ++axis)
                elements *= attr->shape[axis];
            logical_[layer] = elements * sizeof(float16);
            aligned_[layer] = vision.outputs[layer].mem->size;
            if (!elements || aligned_[layer] < logical_[layer] || aligned_[layer] % sizeof(float16))
                return false;
            for (auto &entry : cache_)
                entry.features[layer].resize(aligned_[layer] / sizeof(float16));
            const auto bytes = (FrameCount - 1) * logical_[layer] + aligned_[layer];
            if (layer == 0) {
                primary_.resize(bytes / sizeof(float16));
            } else {
                rknn3_tensor_attr input{};
                input.index = uint32_t(layer + 1);
                if (rknn3_query(context_.llm.rknn_ctx, RKNN3_QUERY_INPUT_ATTR, &input, sizeof(input)) != 0)
                    return false;
                auto *&mem = aux_[layer - 1];
                mem = rknn3_create_mem(context_.llm.rknn_ctx, bytes, input.core_id, RKNN3_FLAG_MEMORY_CACHEABLE);
                if (!mem || !mem->virt_addr || mem->size < bytes)
                    return false;
            }
        }
        return true;
    }

    bool encodeFrame(const QString &path, size_t index, bool &reused, qint64 &preprocessNs)
    {
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
            return false;
        const auto key = hash.result();
        file.close();
        auto entry = std::find_if(cache_.begin(), cache_.end(), [&](const auto &slot) { return slot.key == key; });
        reused = entry != cache_.end();
        if (!reused) {
            entry = std::min_element(cache_.begin(), cache_.end(),
                [](const auto &a, const auto &b) { return a.lastUsed < b.lastUsed; });
            entry->key.clear();
            image_buffer_t image{};
            const auto filename = QFile::encodeName(path);
            const auto read = read_image(filename.constData(), &image);
            std::unique_ptr<void, decltype(&free)> pixels(image.virt_addr, free);
            if (read != 0)
                return false;
            auto &out = entry->features;
            if (rgb_.empty()) {
                if (inference_qwen3_vl_vision(&context_.vision, &image, out[0].data(), out[1].data(),
                                              out[2].data(), out[3].data()) != 0)
                    return false;
            } else if (!encodeIntoBuffers(image, out, preprocessNs)) {
                return false;
            }
            entry->key = key;
        }
        entry->lastUsed = ++sequence_;
        for (size_t layer = 0; layer < logical_.size(); ++layer) {
            auto *destination = layer == 0 ? reinterpret_cast<uchar *>(primary_.data())
                                          : static_cast<uchar *>(aux_[layer - 1]->virt_addr);
            const size_t bytes = index + 1 == FrameCount ? aligned_[layer] : logical_[layer];
            std::memcpy(destination + index * logical_[layer], entry->features[layer].data(), bytes);
        }
        return true;
    }

    float16 *primary() { return primary_.data(); }
    rknn3_tensor_mem *aux(size_t layer) const { return aux_[layer - 1]; }

private:
    static bool canPackDirectly(const rknn_qwen3_vl_vision_context &vision)
    {
        if (vision.pruned_version_flag != 1 || vision.io_num.n_input != 1 ||
            vision.model_width <= 0 || vision.model_height <= 0 ||
            vision.model_width % fieldops::VisionBlock || vision.model_height % fieldops::VisionBlock)
            return false;

        const auto *attr = vision.inputs[0].attr;
        const auto *memory = vision.inputs[0].mem;
        const auto tokens = size_t(vision.model_width / fieldops::VisionPatch) *
                            (vision.model_height / fieldops::VisionPatch);
        return attr && memory && memory->virt_addr && attr->dtype == RKNN3_TENSOR_FLOAT16 &&
               attr->n_dims == 2 && attr->shape[0] == tokens &&
               attr->shape[1] == fieldops::VisionTokenElements &&
               attr->n_stride == 2 && attr->stride[0] == fieldops::VisionTokenElements &&
               attr->stride[1] == 1 && memory->size >= tokens * fieldops::VisionTokenElements * sizeof(float16);
    }

    bool encodeIntoBuffers(image_buffer_t &image, std::array<std::vector<float16>, 4> &features,
                           qint64 &preprocessNs)
    {
        auto &vision = context_.vision;
        auto *input = vision.inputs[0].mem;
        QElapsedTimer timer;
        timer.start();

        image_buffer_t resized{};
        resized.width = vision.model_width;
        resized.height = vision.model_height;
        resized.format = IMAGE_FORMAT_RGB888;
        resized.size = rgb_.size();
        resized.virt_addr = rgb_.data();
        if (convert_image(&image, &resized, nullptr, nullptr, 0) < 0)
            return false;
        fieldops::packVisionInput(rgb_.data(), resized.width, resized.height,
                                  static_cast<float16 *>(input->virt_addr));
        preprocessNs += timer.nsecsElapsed();

        if (rknn3_mem_sync(vision.rknn_ctx, input, RKNN3_MEMORY_SYNC_TO_DEVICE) != RKNN3_SUCCESS ||
            rknn3_run(vision.rknn_ctx, vision.inputs, vision.io_num.n_input,
                      vision.outputs, vision.io_num.n_output) != RKNN3_SUCCESS)
            return false;
        for (size_t layer = 0; layer < features.size(); ++layer) {
            auto *output = vision.outputs[layer].mem;
            if (rknn3_mem_sync(vision.rknn_ctx, output, RKNN3_MEMORY_SYNC_FROM_DEVICE) != RKNN3_SUCCESS)
                return false;
            std::memcpy(features[layer].data(), output->virt_addr, output->size);
        }
        return true;
    }

    struct CachedFrame {
        QByteArray key;
        std::array<std::vector<float16>, 4> features;
        size_t lastUsed = 0;
    };
    rknn_app_context_t &context_;
    std::array<size_t, 4> logical_{}, aligned_{};
    std::array<rknn3_tensor_mem *, 3> aux_{};
    std::array<CachedFrame, FrameCount> cache_;
    std::vector<float16> primary_;
    std::vector<unsigned char> rgb_;
    size_t sequence_ = 0;
};

int runClip(rknn_app_context_t &context, Callbacks &cb, ClipBuffers &buffers, const QJsonObject &request)
{
    const auto files = request["frames"].toArray();
    const auto offsets = request["offsets"].toArray();
    if (files.size() != FrameCount || offsets.size() != FrameCount)
        return -1;

    auto &vision = context.vision;
    auto &llm = context.llm;
    QElapsedTimer clock;
    clock.start();
    QByteArray prompt;
    int reusedFrames = 0;
    qint64 preprocessNs = 0;
    for (size_t i = 0; i < FrameCount; ++i) {
        if (fieldops_cancelled.load())
            return 0;
        bool reused = false;
        if (!buffers.encodeFrame(files[int(i)].toString(), i, reused, preprocessNs))
            return -1;
        reusedFrames += reused;
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
    input.image.image_embed = buffers.primary();
    input.image.n_image = FrameCount;
    input.image.n_image_tokens = vision.embeds_shape[vision.embeds_ndims - 2];
    input.image.image_width = vision.model_width;
    input.image.image_height = vision.model_height;
    input.image.image_start = "<|vision_start|>";
    input.image.image_end = "<|vision_end|>";
    input.image.image_content = "<|image_pad|>";

    for (size_t layer = 1; layer < 4; ++layer) {
        inputs[layer].input_type = RKNN3_LLM_INPUT_AUX;
        inputs[layer].aux_input.mem = buffers.aux(layer);
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
        {"vision_ms", double(visionMs)}, {"inference_ms", double(clock.elapsed())},
        {"preprocess_ms", preprocessNs / 1e6}, {"reused_frames", reusedFrames}}).toJson(QJsonDocument::Compact);
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
    ClipBuffers buffers(context);
    if (!buffers.initialize())
        return 1;
    resident_loop(clock.elapsed(), context.llm.rknn_sess, context.llm.rknn_ctx,
                  [&](const QJsonObject &request) { return runClip(context, cb, buffers, request); });
    return 0;
}
