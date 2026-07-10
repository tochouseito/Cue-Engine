#pragma once

/// ****************************************************************************
/// 全 View で共有する DrawScene GPU リソースの定義
/// ****************************************************************************

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"

// === C++ includes ===
#include <cstdint>
#include <vector>

namespace Cue::DrawSystem
{
    class DrawScene;

    /// @brief 同一 World から抽出した描画入力を View 間で共有する GPU リソース
    class DrawSceneResources final
    {
    public:
        DrawSceneResources(RHI::IBufferManager* a_bufferManager, RHI::IViewManager* a_viewManager,
                           uint32_t a_bufferCount);
        ~DrawSceneResources() = default;
        DrawSceneResources(const DrawSceneResources&) = delete;
        DrawSceneResources& operator=(const DrawSceneResources&) = delete;
        DrawSceneResources(DrawSceneResources&&) = default;
        DrawSceneResources& operator=(DrawSceneResources&&) = default;

        /// @brief Scene 抽出結果を保持する shared buffer を作成する
        [[nodiscard]] Result initialize(uint32_t a_maxObjectCount, uint32_t a_maxCellCount);

        /// @brief Scene 抽出結果をフレーム別 upload buffer へ反映する
        [[nodiscard]] Result upload_draw_scene(uint32_t a_bufferIndex, const DrawScene& a_scene);

        /// @brief RenderableInfo buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle renderable_info_buffer_handle() const noexcept;

        /// @brief Transform buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle transform_buffer_handle() const noexcept;

        /// @brief Material buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle material_buffer_handle() const noexcept;

        /// @brief RenderCell buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle render_cell_buffer_handle() const noexcept;

        /// @brief Transform buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle transform_buffer_srv_handle() const noexcept;

        /// @brief RenderableInfo buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle renderable_info_buffer_srv_handle() const noexcept;

        /// @brief Material buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle material_buffer_srv_handle() const noexcept;

        /// @brief RenderCell buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle render_cell_buffer_srv_handle() const noexcept;

        /// @brief RenderableInfo buffer の upload byte size
        [[nodiscard]] uint64_t renderable_info_buffer_byte_size() const noexcept;

        /// @brief Transform buffer の upload byte size
        [[nodiscard]] uint64_t transform_buffer_byte_size() const noexcept;

    private:
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>> m_renderableInfoUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>> m_transformUploaders{};
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>> m_materialUploaders{};
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>> m_renderCellUploaders{};
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        RHI::BufferHandle m_renderableInfoBuffer{};
        RHI::BufferHandle m_transformBuffer{};
        RHI::BufferHandle m_materialBuffer{};
        RHI::BufferHandle m_renderCellBuffer{};
        RHI::ViewHandle m_renderableInfoBufferSrv{};
        RHI::ViewHandle m_transformBufferSrv{};
        RHI::ViewHandle m_materialBufferSrv{};
        RHI::ViewHandle m_renderCellBufferSrv{};
        uint32_t m_bufferCount = 0;
        uint32_t m_maxObjectCount = 0;
        uint32_t m_maxCellCount = 0;
    };
} // namespace Cue::DrawSystem
