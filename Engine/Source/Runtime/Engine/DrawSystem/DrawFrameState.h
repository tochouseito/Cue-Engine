// DrawFrameState の役割と公開要素を定義する

#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <vector>

namespace Cue::DrawSystem
{
    struct CpuIndexedDraw final
    {
        uint32_t renderObjectId = 0;
        uint32_t indexCount = 0;
        uint32_t startIndex = 0;
        int32_t baseVertex = 0;
        float sortDepth = 0.0f;
    };

    struct CpuShadowCaster final
    {
        Math::float3 center = Math::float3::zero();
        float radius = 0.0f;
    };

    struct DrawFrameData final
    {
        uint32_t objectCount = 0;
        uint32_t spriteCount = 0;
        uint32_t renderWidth = 1;
        uint32_t renderHeight = 1;
        bool useCpuBatching = false;
        std::vector<CpuIndexedDraw> cpuIndexedDraws{};
        std::vector<CpuIndexedDraw> transparentCpuIndexedDraws{};
        std::vector<CpuShadowCaster> cpuShadowCasters{};
    };

    struct DrawFrameState final
    {
        void resize(const uint32_t a_bufferCount)
        {
            frameStates.resize(a_bufferCount);
        }

        DrawFrameData& frame_state(const uint32_t a_bufferIndex) noexcept
        {
            return frameStates[a_bufferIndex];
        }

        const DrawFrameData& frame_state(const uint32_t a_bufferIndex) const noexcept
        {
            return frameStates[a_bufferIndex];
        }

        std::vector<DrawFrameData> frameStates{};
    };
} // namespace Cue::DrawSystem
