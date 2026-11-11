#pragma once

#include "float16.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace fieldops {
constexpr int VisionPatch = 16;
constexpr int VisionMerge = 2;
constexpr int VisionBlock = VisionPatch * VisionMerge;
constexpr int VisionChannels = 3;
constexpr int VisionTemporal = 2;
constexpr int VisionPatchElements = VisionPatch * VisionPatch;
constexpr int VisionTokenElements = VisionChannels * VisionTemporal * VisionPatchElements;

// The vendor encoder expects merged 2x2 patch groups ordered by channel and
// temporal index. A still image supplies the same patch at both temporal indices.
inline void packVisionInput(const unsigned char *rgb, int width, int height, float16 *output)
{
    static const auto normalized = [] {
        std::array<float16, 256> table{};
        for (std::size_t i = 0; i < table.size(); ++i)
            table[i] = fp32_to_fp16((static_cast<float>(i) - 127.5f) / 127.5f);
        return table;
    }();

    for (int blockY = 0; blockY < height; blockY += VisionBlock) {
        for (int blockX = 0; blockX < width; blockX += VisionBlock) {
            for (int patchY = 0; patchY < VisionBlock; patchY += VisionPatch) {
                for (int patchX = 0; patchX < VisionBlock; patchX += VisionPatch) {
                    auto *red = output;
                    auto *green = red + VisionTemporal * VisionPatchElements;
                    auto *blue = green + VisionTemporal * VisionPatchElements;
                    const auto *patch = rgb + ((blockY + patchY) * width + blockX + patchX) * VisionChannels;
                    for (int y = 0; y < VisionPatch; ++y) {
                        const auto *pixel = patch + y * width * VisionChannels;
                        const int row = y * VisionPatch;
                        for (int x = 0; x < VisionPatch; ++x, pixel += VisionChannels) {
                            red[row + x] = normalized[pixel[0]];
                            green[row + x] = normalized[pixel[1]];
                            blue[row + x] = normalized[pixel[2]];
                        }
                    }
                    constexpr auto bytes = VisionPatchElements * sizeof(float16);
                    std::memcpy(red + VisionPatchElements, red, bytes);
                    std::memcpy(green + VisionPatchElements, green, bytes);
                    std::memcpy(blue + VisionPatchElements, blue, bytes);
                    output += VisionTokenElements;
                }
            }
        }
    }
}
}
