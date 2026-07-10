#pragma once

/// ****************************************************************************
/// View ごとに保持する camera GPU リソースの定義
/// ****************************************************************************

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include "DrawSystem/RenderView.h"

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

namespace Cue::DrawSystem
{
    /// @brief RenderView を GPU constant buffer へ転送する View 固有リソース
    class DrawViewResources final
    {
    public:
        DrawViewResources(RHI::IBufferManager* a_bufferManager, uint32_t a_bufferCount, std::string a_name);
        ~DrawViewResources() = default;
        DrawViewResources(const DrawViewResources&) = delete;
        DrawViewResources& operator=(const DrawViewResources&) = delete;
        DrawViewResources(DrawViewResources&&) = default;
        DrawViewResources& operator=(DrawViewResources&&) = default;

        /// @brief ViewProjection constant buffer を作成する
        [[nodiscard]] Result initialize();

        /// @brief RenderView をフレーム別 upload buffer へ反映する
        [[nodiscard]] Result upload_view(uint32_t a_bufferIndex, const RenderView& a_view);

        /// @brief ViewProjection constant buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle view_projection_buffer_handle() const noexcept;

        /// @brief ViewProjection constant buffer の upload byte size
        [[nodiscard]] uint64_t view_projection_buffer_byte_size() const noexcept;

    private:
        std::string m_name{};
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>> m_viewProjectionUploaders{};
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::BufferHandle m_viewProjectionBuffer{};
        uint32_t m_bufferCount = 0;
    };
} // namespace Cue::DrawSystem
