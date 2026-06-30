#pragma once

/// ****************************************************************************
/// ワールド全体で共有される描画リソースの定義
/// *****************************************************************************

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "GpuData/ViewProjection.h"

// === C++ includes ===
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Cue::DrawSystem
{
    class DrawScene;
    struct DrawFrameData;

    /// @brief DrawResources が管理する共有描画リソースの種別
    enum class DrawResourceType : uint32_t
    {
        RenderableInfoBuffer = 0, // 描画対象の mesh/material/transform 参照情報
        TransformBuffer,          // オブジェクトごとのワールド変換情報
        ViewProjectionBuffer,     // カメラの view/projection 定数バッファ
        MaterialBuffer,           // マテリアルパラメータ配列
        RenderCellBuffer,         // 空間分割セル情報
        RenderObjectBuffer,       // 可視判定後の描画オブジェクト出力
        VisibleObjectCountBuffer, // 可視オブジェクト数の UAV counter
        StaticMeshIndirectCommandBuffer,      // StaticMesh の ExecuteIndirect 引数
        StaticMeshIndirectCommandCountBuffer, // StaticMesh の ExecuteIndirect 発行数
        StaticMeshObjectIndexBuffer,          // バッチ順に並べた DrawScene object index
        Count                     // 配列サイズ用
    };

    /// @brief ワールド全体で共有する描画用 GPU buffer / view / uploader を管理するクラス
    class DrawResources final
    {
    public:
        /// @brief 外部 RHI manager とフレーム分の upload buffer 数を受け取る
        DrawResources(RHI::IBufferManager* bufferManager, RHI::IViewManager* viewManager, uint32_t a_bufferCount)
            : m_bufferManager(bufferManager), m_viewManager(viewManager), m_bufferCount(a_bufferCount)
        {
        }
        ~DrawResources() = default;
        DrawResources(const DrawResources&) = delete;
        DrawResources& operator=(const DrawResources&) = delete;
        DrawResources(DrawResources&&) = default;
        DrawResources& operator=(DrawResources&&) = default;

        // ワールド全体で共有されるリソース

        /// @brief RenderableInfo 用 structured buffer、uploaders、SRV を作成する
        Result create_renderable_info_buffer(const uint32_t a_maxObjectCount);

        /// @brief ObjectTransform 用 structured buffer、uploaders、SRV を作成する
        Result create_transform_buffer(const uint32_t a_maxObjectCount);

        /// @brief ViewProjection 用 constant buffer と uploaders を作成する
        Result create_view_projection_buffer();

        /// @brief MaterialGpu 用 structured buffer、uploaders、SRV を作成する
        Result create_material_buffer(const uint32_t a_maxMaterialCount);

        /// @brief RenderCellGpu 用 structured buffer、uploaders、SRV を作成する
        Result create_render_cell_buffer(const uint32_t a_maxCellCount);

        /// @brief RenderObject 用 UAV buffer、uploaders、UAV を作成する
        Result create_render_object_buffer(const uint32_t a_maxObjectCount);

        /// @brief 可視オブジェクト数を保持する raw UAV buffer、uploaders、UAV を作成する
        Result create_object_count_buffer();

        /// @brief StaticMesh indirect draw 用の command / count / object index buffer を作成する
        Result create_static_mesh_batch_buffers(uint32_t a_maxBatchCount, uint32_t a_maxObjectIndexCount);

        /// @brief DrawScene の RenderableInfo / Transform をフレーム別 upload buffer に反映する
        Result upload_draw_scene(uint32_t a_bufferIndex, const DrawScene& a_scene, DrawFrameData& a_frameData);

        /// @brief RenderableInfo buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>& renderable_info_uploaders() noexcept
        {
            return m_renderableInfoUploaders;
        }

        /// @brief ObjectTransform buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>& transform_uploaders() noexcept
        {
            return m_transformUploaders;
        }

        /// @brief RenderObject buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::RenderObject>>& render_object_uploaders() noexcept
        {
            return m_renderObjectUploaders;
        }

        /// @brief Material buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>>& material_uploaders() noexcept
        {
            return m_materialUploaders;
        }

        /// @brief RenderCell buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>>& render_cell_uploaders() noexcept
        {
            return m_renderCellUploaders;
        }

        /// @brief VisibleObjectCount buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<uint32_t>>& visible_object_count_uploaders() noexcept
        {
            return m_visibleObjectCountUploaders;
        }

        /// @brief ViewProjection buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>& view_projection_uploaders() noexcept
        {
            return m_viewProjectionUploaders;
        }

        /// @brief StaticMesh indirect command buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<GpuData::IndirectCommand>>& static_mesh_indirect_command_uploaders() noexcept;

        /// @brief StaticMesh indirect command count buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<uint32_t>>& static_mesh_indirect_command_count_uploaders() noexcept;

        /// @brief StaticMesh object index buffer のフレーム別 uploader 配列
        std::vector<RHI::SlotUploader<uint32_t>>& static_mesh_object_index_uploaders() noexcept;

        /// @brief RenderableInfo buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle renderable_info_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)];
        }

        /// @brief Transform buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle transform_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::TransformBuffer)];
        }

        /// @brief ViewProjection constant buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle view_projection_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::ViewProjectionBuffer)];
        }

        /// @brief Material buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle material_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::MaterialBuffer)];
        }

        /// @brief RenderCell buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle render_cell_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderCellBuffer)];
        }

        /// @brief RenderObject UAV buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle render_object_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderObjectBuffer)];
        }

        /// @brief VisibleObjectCount raw buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle visible_object_count_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(DrawResourceType::VisibleObjectCountBuffer)];
        }

        /// @brief StaticMesh indirect command buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle static_mesh_indirect_command_buffer_handle() const noexcept;

        /// @brief StaticMesh indirect command count buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle static_mesh_indirect_command_count_buffer_handle() const noexcept;

        /// @brief StaticMesh object index buffer の RHI handle
        [[nodiscard]] RHI::BufferHandle static_mesh_object_index_buffer_handle() const noexcept;

        /// @brief RenderableInfo buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle renderable_info_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)];
        }

        /// @brief Transform buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle transform_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(DrawResourceType::TransformBuffer)];
        }

        /// @brief Material buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle material_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(DrawResourceType::MaterialBuffer)];
        }

        /// @brief RenderCell buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle render_cell_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(DrawResourceType::RenderCellBuffer)];
        }

        /// @brief RenderObject buffer の UAV handle
        [[nodiscard]] RHI::ViewHandle render_object_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(DrawResourceType::RenderObjectBuffer)];
        }

        /// @brief VisibleObjectCount buffer の UAV handle
        [[nodiscard]] RHI::ViewHandle visible_object_count_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(DrawResourceType::VisibleObjectCountBuffer)];
        }

        /// @brief StaticMesh object index buffer の SRV handle
        [[nodiscard]] RHI::ViewHandle static_mesh_object_index_buffer_srv_handle() const noexcept;

        /// @brief RenderableInfoBuffer の確保済み byte 数
        [[nodiscard]] uint64_t renderable_info_buffer_byte_size() const noexcept
        {
            return static_cast<uint64_t>(m_maxRenderableInfoCount) * sizeof(GpuData::RenderableInfo);
        }

        /// @brief TransformBuffer の確保済み byte 数
        [[nodiscard]] uint64_t transform_buffer_byte_size() const noexcept
        {
            return static_cast<uint64_t>(m_maxTransformCount) * sizeof(GpuData::ObjectTransformGpu);
        }

        /// @brief ViewProjectionBuffer の byte 数
        [[nodiscard]] uint64_t view_projection_buffer_byte_size() const noexcept
        {
            return sizeof(GpuData::ViewProjectionGpu);
        }

    private:
        RHI::IBufferManager* m_bufferManager = nullptr; // buffer の生成と uploader 作成を行う外部 manager
        RHI::IViewManager* m_viewManager = nullptr;     // SRV/UAV view の生成を行う外部 manager
        uint32_t m_bufferCount = 1;                     // フレームごとに用意する upload heap / uploader 数
        uint32_t m_maxRenderableInfoCount = 0;          // RenderableInfoBuffer の最大要素数
        uint32_t m_maxTransformCount = 0;               // TransformBuffer の最大要素数
        uint32_t m_maxStaticMeshBatchCount = 0;         // StaticMesh indirect command の最大要素数
        uint32_t m_maxStaticMeshObjectIndexCount = 0;   // StaticMesh object index の最大要素数

        std::array<RHI::BufferHandle, static_cast<size_t>(DrawResourceType::Count)>
            m_bufferHandles{}; // 種別ごとの GPU buffer handle
        std::array<RHI::ViewHandle, static_cast<size_t>(DrawResourceType::Count)>
            m_viewHandles{}; // 種別ごとの SRV/UAV handle
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>
            m_renderableInfoUploaders{}; // RenderableInfo buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>
            m_transformUploaders{}; // Transform buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>>
            m_materialUploaders{}; // Material buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>>
            m_renderCellUploaders{}; // RenderCell buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::RenderObject>>
            m_renderObjectUploaders{}; // RenderObject buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<uint32_t>>
            m_visibleObjectCountUploaders{}; // VisibleObjectCount buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>
            m_viewProjectionUploaders{}; // ViewProjection buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::IndirectCommand>>
            m_staticMeshIndirectCommandUploaders{}; // StaticMesh indirect command buffer への upload 経路
        std::vector<RHI::SlotUploader<uint32_t>>
            m_staticMeshIndirectCommandCountUploaders{}; // StaticMesh indirect command count への upload 経路
        std::vector<RHI::SlotUploader<uint32_t>>
            m_staticMeshObjectIndexUploaders{}; // StaticMesh object index buffer への upload 経路
    };
} // namespace Cue::DrawSystem
