#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

// Private pipe protocol between the board's face worker and Qt on the same host.
struct FaceObservationPacket {
    static constexpr int PointCount = 478;
    enum : uint32_t { Mesh = 1, Angles = 2, Embedding = 4 };
    std::array<float, 4> bounds{};
    float score = 0;
    uint32_t flags = 0;
    std::array<float, 5> angles{};
    std::array<float, 2> rawHead{};
    std::array<float, 3> headDirection{}, gazeDirection{};
    std::array<float, 2> headOrigin{}, gazeOrigin{};
    std::array<float, PointCount * 2> points{};
    std::array<float, 128> embedding{};
};
static_assert(sizeof(float) == 4 && std::is_trivially_copyable_v<FaceObservationPacket>);
