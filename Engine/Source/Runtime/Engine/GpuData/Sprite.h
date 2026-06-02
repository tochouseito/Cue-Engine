// Sprite の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdint>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    struct SpriteInstanceGpu final
    {
        Math::float4 positionSize = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        Math::float4 uvRect = Math::float4(0.0f, 0.0f, 1.0f, 1.0f);
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        uint32_t textureId = 0;
        uint32_t useTexture = 0;
        float rotation = 0.0f;
        uint32_t padding0 = 0;
        Math::float2 pivot = Math::float2(0.5f, 0.5f);
        Math::float2 padding1 = Math::float2(0.0f, 0.0f);
    };
}
