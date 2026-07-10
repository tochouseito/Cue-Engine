#pragma once

/// ****************************************************************************
/// View ごとの culling と indirect draw 用 GPU リソースの定義
/// ****************************************************************************

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "GpuData/Batching.h"

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

namespace Cue::DrawSystem
{
    /// @brief camera ごとに異なる可視集合と indirect draw 引数を保持する GPU リソース
    class DrawVisibilityResources final
    {
    public:
        DrawVisibilityResources(RHI::IBufferManager* a_bufferManager, RHI::IViewManager* a_viewManager,
                                uint32_t a_bufferCount, std::string a_name);
        ~DrawVisibilityResources() = default;
        DrawVisibilityResources(const DrawVisibilityResources&) = delete;
        DrawVisibilityResources& operator=(const DrawVisibilityResources&) = delete;
        DrawVisibilityResources(DrawVisibilityResources&&) = default;
        DrawVisibilityResources& operator=(DrawVisibilityResources&&) = default;

        /// @brief visibility と indirect draw 用の View 固有 buffer を作成する
        [[nodiscard]] Result initialize(uint32_t a_maxObjectCount, uint32_t a_maxBatchCount,
                                        uint32_t a_maxObjectIndexCount);

        /// @brief CPU batching 結果をフレーム別 upload buffer へ反映する
        [[nodiscard]] Result upload_visibility(uint32_t a_bufferIndex, const DrawFrameData& a_frameData);

        /// @brief RenderObject UAV buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle render_object_buffer_handle() const noexcept;

        /// @brief VisibleObjectCount UAV buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle visible_object_count_buffer_handle() const noexcept;

        /// @brief RenderObject buffer の UAV handle
        [[nodiscard]] RHI::ViewHandle render_object_buffer_uav_handle() const noexcept;

        /// @brief VisibleObjectCount buffer の UAV handle
        [[nodiscard]] RHI::ViewHandle visible_object_count_buffer_uav_handle() const noexcept;

        /// @brief StaticMesh indirect command buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle static_mesh_indirect_command_buffer_handle() const noexcept;

        /// @brief StaticMesh indirect command count buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle static_mesh_indirect_command_count_buffer_handle() const noexcept;

        /// @brief StaticMesh object index buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle static_mesh_object_index_buffer_handle() const noexcept;

        /// @brief StaticMesh object index buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle static_mesh_object_index_buffer_srv_handle() const noexcept;

        /// @brief StaticMesh indirect command buffer の upload byte size
        [[nodiscard]] uint64_t static_mesh_indirect_command_buffer_byte_size() const noexcept;

        /// @brief StaticMesh indirect command count buffer の upload byte size
        [[nodiscard]] uint64_t static_mesh_indirect_command_count_buffer_byte_size() const noexcept;

        /// @brief StaticMesh object index buffer の upload byte size
        [[nodiscard]] uint64_t static_mesh_object_index_buffer_byte_size() const noexcept;

    private:
        std::string m_name{};
        std::vector<RHI::SlotUploader<GpuData::RenderObject>> m_renderObjectUploaders{};
        std::vector<RHI::SlotUploader<uint32_t>> m_visibleObjectCountUploaders{};
        std::vector<RHI::SlotUploader<GpuData::IndirectCommand>> m_staticMeshIndirectCommandUploaders{};
        std::vector<RHI::SlotUploader<uint32_t>> m_staticMeshIndirectCommandCountUploaders{};
        std::vector<RHI::SlotUploader<uint32_t>> m_staticMeshObjectIndexUploaders{};
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_staticMeshIndirectCommandBuffer{};
        RHI::BufferHandle m_staticMeshIndirectCommandCountBuffer{};
        RHI::BufferHandle m_staticMeshObjectIndexBuffer{};
        RHI::ViewHandle m_renderObjectBufferUav{};
        RHI::ViewHandle m_visibleObjectCountBufferUav{};
        RHI::ViewHandle m_staticMeshObjectIndexBufferSrv{};
        uint32_t m_bufferCount = 0;
        uint32_t m_maxObjectCount = 0;
        uint32_t m_maxBatchCount = 0;
        uint32_t m_maxObjectIndexCount = 0;
    };
} // namespace Cue::DrawSystem
