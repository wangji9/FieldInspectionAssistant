#pragma once

#include <rknn3_api.h>
#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

class Rk1828Model {
public:
    Rk1828Model() = default;
    Rk1828Model(const Rk1828Model &) = delete;
    Rk1828Model &operator=(const Rk1828Model &) = delete;

    ~Rk1828Model() {
        for (auto &output : outputs_)
            if (output.mem)
                rknn3_destroy_mem(context_, output.mem);
        if (input_.mem)
            rknn3_destroy_mem(context_, input_.mem);
        if (context_)
            rknn3_destroy(context_);
    }

    bool load(const char *path, std::initializer_list<unsigned> sizes) {
        const std::string model(path);
        if (model.size() < 5 || model.substr(model.size() - 5) != ".rknn")
            return false;
        const auto weight = model.substr(0, model.size() - 5) + ".weight";
        rknn3_config config{};
        const char *core = std::getenv("FIELDOPS_VISION_CORE_MASK");
        config.run_core_mask = core ? std::strtoul(core, nullptr, 0) : 0x2;
        config.run_timeout = 5000;
        rknn3_input_output_num io{};
        if (rknn3_init(&context_, nullptr) != 0
            || rknn3_load_model_from_path(context_, path, weight.c_str()) != 0
            || rknn3_model_init(context_, &config) != 0
            || rknn3_query(context_, RKNN3_QUERY_IN_OUT_NUM, &io, sizeof(io)) != 0
            || io.n_input != 1 || io.n_output != sizes.size()
            || !createTensor(input_, inputAttr_, RKNN3_QUERY_INPUT_ATTR, 0))
            return false;
        if (inputAttr_.layout != RKNN3_TENSOR_NHWC || inputAttr_.dtype != RKNN3_TENSOR_UINT8
            || inputAttr_.n_dims != 4 || inputAttr_.shape[0] != 1 || inputAttr_.shape[3] != 3
            || !strides(inputAttr_, inputStride_) || inputStride_[3] != 1)
            return false;
        outputAttrs_.resize(io.n_output);
        outputs_.resize(io.n_output);
        outputStrides_.resize(io.n_output);
        values_.resize(io.n_output);
        unsigned index = 0;
        for (unsigned size : sizes) {
            auto &attr = outputAttrs_[index];
            if (!createTensor(outputs_[index], attr, RKNN3_QUERY_OUTPUT_ATTR, index)
                || attr.n_elems != size || attr.layout == RKNN3_TENSOR_NC1HWC2
                || !strides(attr, outputStrides_[index]))
                return false;
            values_[index].resize(size);
            ++index;
        }
        return true;
    }

    bool infer(const cv::Mat &image) {
        if (image.type() != CV_8UC3 || image.rows != int(inputAttr_.shape[1])
            || image.cols != int(inputAttr_.shape[2]))
            return false;
        auto *destination = static_cast<uint8_t *>(input_.mem->virt_addr);
        const size_t rowBytes = image.cols * 3;
        if (image.isContinuous() && inputStride_[1] == rowBytes && inputStride_[2] == 3) {
            std::memcpy(destination, image.data, rowBytes * image.rows);
        } else {
            for (int y = 0; y < image.rows; ++y) {
                auto *row = destination + y * inputStride_[1];
                const auto *source = image.ptr<uint8_t>(y);
                if (inputStride_[2] == 3) {
                    std::memcpy(row, source, rowBytes);
                } else {
                    for (int x = 0; x < image.cols; ++x)
                        std::memcpy(row + x * inputStride_[2], source + x * 3, 3);
                }
            }
        }
        if (rknn3_mem_sync(context_, input_.mem, RKNN3_MEMORY_SYNC_TO_DEVICE) != 0
            || rknn3_run(context_, &input_, 1, outputs_.data(), outputs_.size()) != 0)
            return false;
        for (unsigned i = 0; i < outputs_.size(); ++i) {
            if (rknn3_mem_sync(context_, outputs_[i].mem, RKNN3_MEMORY_SYNC_FROM_DEVICE) != 0 || !unpack(i))
                return false;
        }
        return true;
    }

    const float *output(unsigned index) const { return values_[index].data(); }
    const rknn3_tensor_attr &outputAttr(unsigned index) const { return outputAttrs_[index]; }
    int width() const { return inputAttr_.shape[2]; }
    int height() const { return inputAttr_.shape[1]; }

private:
    using Strides = std::array<size_t, RKNN3_MAX_DIMS>;

    bool createTensor(rknn3_tensor &tensor, rknn3_tensor_attr &attr, rknn3_query_cmd query, unsigned index) {
        attr.index = index;
        if (rknn3_query(context_, query, &attr, sizeof(attr)) != 0 || !attr.aligned_size)
            return false;
        tensor.attr = &attr;
        tensor.mem = rknn3_create_mem(context_, attr.aligned_size, attr.core_id, RKNN3_FLAG_MEMORY_CACHEABLE);
        return tensor.mem && tensor.mem->virt_addr;
    }

    static bool strides(const rknn3_tensor_attr &attr, Strides &out) {
        if (attr.n_dims == 0 || attr.n_dims > out.size())
            return false;
        if (attr.n_stride != attr.n_dims && attr.n_stride != attr.n_dims + 1 && attr.n_stride != 0)
            return false;
        const unsigned lead = attr.n_stride == attr.n_dims + 1 ? 1 : 0;
        size_t dense = 1;
        for (unsigned i = attr.n_dims; i-- > 0;) {
            if (attr.shape[i] == 0)
                return false;
            out[i] = attr.n_stride ? attr.stride[i + lead] : dense;
            dense *= attr.shape[i];
        }
        return true;
    }

    template<class T>
    void unpackRows(unsigned index, float scale, float zero) {
        const auto &attr = outputAttrs_[index];
        const auto &stride = outputStrides_[index];
        const auto *source = static_cast<const T *>(outputs_[index].mem->virt_addr);
        auto *destination = values_[index].data();
        const unsigned last = attr.n_dims - 1;
        const unsigned columns = attr.shape[last];
        const size_t rows = values_[index].size() / columns;
        for (size_t row = 0; row < rows; ++row) {
            size_t coordinate = row, offset = 0;
            for (unsigned axis = last; axis-- > 0;) {
                offset += coordinate % attr.shape[axis] * stride[axis];
                coordinate /= attr.shape[axis];
            }
            if (stride[last] == 1) {
                for (unsigned col = 0; col < columns; ++col)
                    *destination++ = (static_cast<float>(source[offset + col]) - zero) * scale;
            } else {
                for (unsigned col = 0; col < columns; ++col)
                    *destination++ = (static_cast<float>(source[offset + col * stride[last]]) - zero) * scale;
            }
        }
    }

    bool unpack(unsigned index) {
        const auto &attr = outputAttrs_[index];
        const float scale = attr.qnt_type == RKNN3_TENSOR_QNT_NONE ? 1.f : attr.qnt_info.scale;
        const float zero = attr.qnt_type == RKNN3_TENSOR_QNT_NONE ? 0.f : attr.qnt_info.zero_point;
        switch (attr.dtype) {
        case RKNN3_TENSOR_FLOAT32: unpackRows<float>(index, 1, 0); break;
        case RKNN3_TENSOR_FLOAT16: unpackRows<__fp16>(index, 1, 0); break;
        case RKNN3_TENSOR_INT8: unpackRows<int8_t>(index, scale, zero); break;
        case RKNN3_TENSOR_UINT8: unpackRows<uint8_t>(index, scale, zero); break;
        case RKNN3_TENSOR_INT16: unpackRows<int16_t>(index, scale, zero); break;
        default: return false;
        }
        return true;
    }

    rknn3_context context_ = 0;
    rknn3_tensor input_{};
    rknn3_tensor_attr inputAttr_{};
    Strides inputStride_{};
    std::vector<rknn3_tensor> outputs_;
    std::vector<rknn3_tensor_attr> outputAttrs_;
    std::vector<Strides> outputStrides_;
    std::vector<std::vector<float>> values_;
};
