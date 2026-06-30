#pragma once

/// ****************************************************************************
/// 1 フレームの描画視点スナップショット
/// ****************************************************************************

// === Math includes ===
#include <CueMath.h>

// === Engine includes ===
#include "GpuData/ViewProjection.h"

// === C++ includes ===
#include <cstdint>

namespace Cue::DrawSystem
{
    struct RenderView final
    {
        // 描画時点で確定した view/projection。GameCore の CameraComponent は保持しない。
        Math::float4x4 view = Math::float4x4::identity();
        Math::float4x4 projection = Math::float4x4::identity();
        // shader の cameraPosition など、view 行列から復元しない値を保持する。
        Math::float3 position = Math::float3::zero();

        // camera が対象にする描画解像度。aspect や viewport の基準にする。
        uint32_t width = 1;
        uint32_t height = 1;

        // projection 構築時に使った depth range。後続 pass の参照用に残す。
        float nearZ = 0.1f;
        float farZ = 1000.0f;
    };

    /// @brief CPU 側の RenderView を GPU constant buffer 用 layout へ変換する。
    [[nodiscard]] inline GpuData::ViewProjectionGpu make_view_projection_gpu(
        const RenderView& a_view) noexcept
    {
        GpuData::ViewProjectionGpu gpu{};
        gpu.view = a_view.view;
        gpu.projection = a_view.projection;
        gpu.cameraPosition =
            Math::float4(a_view.position.x, a_view.position.y, a_view.position.z, 1.0f);
        return gpu;
    }
} // namespace Cue::DrawSystem
