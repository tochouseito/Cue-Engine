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
#include <vector>

namespace Cue::DrawSystem
{
    /// @brief DrawResources が管理する共有描画リソースの種別
    enum class DrawResourceType : uint32_t
    {
        RenderableInfoBuffer = 0, // 描画対象の mesh/material/transform 参照情報
        TransformBuffer, // オブジェクトごとのワールド変換情報
        ViewProjectionBuffer, // カメラの view/projection 定数バッファ
        MaterialBuffer, // マテリアルパラメータ配列
        RenderCellBuffer, // 空間分割セル情報
        RenderObjectBuffer, // 可視判定後の描画オブジェクト出力
        VisibleObjectCountBuffer, // 可視オブジェクト数の UAV counter
        Count // 配列サイズ用
    };

    /// @brief ワールド全体で共有する描画用 GPU buffer / view / uploader を管理するクラス
    class DrawResources final
    {
    public:
        /// @brief 外部 RHI manager とフレーム分の upload buffer 数を受け取る
        DrawResources(RHI::IBufferManager* bufferManager,
            RHI::IViewManager* viewManager,
            uint32_t a_bufferCount)
            : m_bufferManager(bufferManager)
            , m_viewManager(viewManager)
            , m_bufferCount(a_bufferCount)
        {}
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

        /// @brief RenderableInfo buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>&
            renderable_info_uploaders() noexcept
        {
            return m_renderableInfoUploaders;
        }

        /// @brief ObjectTransform buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>& transform_uploaders() noexcept
        {
            return m_transformUploaders;
        }

        /// @brief RenderObject buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<GpuData::RenderObject>>&
            render_object_uploaders() noexcept
        {
            return m_renderObjectUploaders;
        }

        /// @brief Material buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>>&
            material_uploaders() noexcept
        {
            return m_materialUploaders;
        }

        /// @brief RenderCell buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>>&
            render_cell_uploaders() noexcept
        {
            return m_renderCellUploaders;
        }

        /// @brief VisibleObjectCount buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<uint32_t>>&
            visible_object_count_uploaders() noexcept
        {
            return m_visibleObjectCountUploaders;
        }

        /// @brief ViewProjection buffer のフレーム別 uploader 配列を返す
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>&
            view_projection_uploaders() noexcept
        {
            return m_viewProjectionUploaders;
        }

        /// @brief RenderableInfo buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle renderable_info_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::RenderableInfoBuffer)];
        }

        /// @brief Transform buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle transform_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::TransformBuffer)];
        }

        /// @brief ViewProjection constant buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle view_projection_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::ViewProjectionBuffer)];
        }

        /// @brief Material buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle material_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::MaterialBuffer)];
        }

        /// @brief RenderCell buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle render_cell_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::RenderCellBuffer)];
        }

        /// @brief RenderObject UAV buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle render_object_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::RenderObjectBuffer)];
        }

        /// @brief VisibleObjectCount raw buffer の RHI handle を返す
        [[nodiscard]] RHI::BufferHandle visible_object_count_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::VisibleObjectCountBuffer)];
        }

        /// @brief RenderableInfo buffer の SRV handle を返す
        [[nodiscard]] RHI::ViewHandle renderable_info_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::RenderableInfoBuffer)];
        }

        /// @brief Transform buffer の SRV handle を返す
        [[nodiscard]] RHI::ViewHandle transform_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::TransformBuffer)];
        }

        /// @brief Material buffer の SRV handle を返す
        [[nodiscard]] RHI::ViewHandle material_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::MaterialBuffer)];
        }

        /// @brief RenderCell buffer の SRV handle を返す
        [[nodiscard]] RHI::ViewHandle render_cell_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::RenderCellBuffer)];
        }

        /// @brief RenderObject buffer の UAV handle を返す
        [[nodiscard]] RHI::ViewHandle render_object_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::RenderObjectBuffer)];
        }

        /// @brief VisibleObjectCount buffer の UAV handle を返す
        [[nodiscard]] RHI::ViewHandle visible_object_count_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::VisibleObjectCountBuffer)];
        }
    private:
        RHI::IBufferManager* m_bufferManager = nullptr; // buffer の生成と uploader 作成を行う外部 manager
        RHI::IViewManager* m_viewManager = nullptr; // SRV/UAV view の生成を行う外部 manager
        uint32_t m_bufferCount = 1; // フレームごとに用意する upload heap / uploader 数

        std::array<RHI::BufferHandle, static_cast<size_t>(DrawResourceType::Count)> m_bufferHandles{}; // 種別ごとの GPU buffer handle
        std::array<RHI::ViewHandle, static_cast<size_t>(DrawResourceType::Count)> m_viewHandles{}; // 種別ごとの SRV/UAV handle
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>
            m_renderableInfoUploaders{}; // RenderableInfo buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>> m_transformUploaders{}; // Transform buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>> m_materialUploaders{}; // Material buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>> m_renderCellUploaders{}; // RenderCell buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::RenderObject>> m_renderObjectUploaders{}; // RenderObject buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<uint32_t>> m_visibleObjectCountUploaders{}; // VisibleObjectCount buffer へのフレーム別 upload 経路
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>> m_viewProjectionUploaders{}; // ViewProjection buffer へのフレーム別 upload 経路
    };
}
