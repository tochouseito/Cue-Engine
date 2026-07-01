#pragma once

/// ************************************************************************************
/// DrawSystem に渡す CPU 側描画リスト
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "RenderView.h"

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Cue::DrawSystem
{
    /// @brief DrawSystem 内で扱う描画キュー。
    ///
    /// GameCore 側の RenderQueue を DrawSystem の分類へ写した値。
    enum class DrawRenderQueue : uint8_t
    {
        // 深度前提の不透明描画
        Opaque = 0,
        // 後段でソートやブレンド処理が必要な透明描画
        Transparent = 1,
    };

    /// @brief shadow map pass での StaticMesh 描画方法。
    enum class DrawShadowCasterMode : uint8_t
    {
        // 通常の片面 shadow caster
        Solid = 0,
        // 両面を shadow caster として扱う
        TwoSided = 1,
    };

    /// @brief StaticMesh 1 個分の CPU 側描画情報。
    ///
    /// GPU に直接 upload する構造とは分け、CPU batching や queue
    /// 分類で必要な情報を保持する。
    struct StaticMeshDrawObject final
    {
        // 元 EntityId。DrawSystem からは opaque な追跡用 ID として扱う
        uint32_t sourceEntityId = 0;
        // RenderableInfoBuffer 上の index
        uint32_t objectId = 0;
        // TransformBuffer 上の index
        uint32_t transformId = 0;
        // MeshPool 上の mesh ID
        uint32_t meshId = 0;
        // Material buffer 上の material ID
        uint32_t materialId = 0;
        // 不透明/透明の描画キュー
        DrawRenderQueue renderQueue = DrawRenderQueue::Opaque;
        // shadow map pass での描画方法
        DrawShadowCasterMode shadowCasterMode = DrawShadowCasterMode::Solid;
        // 通常描画に含めるか
        bool visible = true;
        // shadow caster として扱うか
        bool castsShadow = true;
        // shadow receiver として扱うか
        bool receivesShadow = true;
    };

    /// @brief CameraSystem が DrawSystem へ渡す 1 カメラ分の描画視点。
    struct CameraDrawItem final
    {
        // CameraComponent から変換済みの描画入力。GameCore への依存をここで断つ。
        RenderView renderView{};
        // 複数 camera がある場合、ViewProjectionBuffer に採用する候補を示す。
        bool isMain = false;
    };

    /// @brief 1 フレーム分の DrawSystem 入力を保持する。
    ///
    /// StaticMeshDrawObject、RenderableInfo、ObjectTransformGpu は同じ index
    /// で対応する。
    class DrawScene final
    {
    public:
        DrawScene();
        ~DrawScene();

        DrawScene(const DrawScene&);
        DrawScene& operator=(const DrawScene&);
        DrawScene(DrawScene&&) noexcept;
        DrawScene& operator=(DrawScene&&) noexcept;

        /// @brief 保持している描画リストを空にする。
        void clear() noexcept;

        /// @brief StaticMesh 描画対象数。
        [[nodiscard]] size_t object_count() const noexcept;

        /// @brief StaticMesh 描画対象と GPU upload 用データを同じ index に追加する。
        [[nodiscard]] Result add_static_mesh_object(const StaticMeshDrawObject& a_object,
            const GpuData::RenderableInfo& a_renderableInfo,
            const GpuData::ObjectTransformGpu& a_transform);

        /// @brief Camera 描画視点を追加する。
        [[nodiscard]] Result add_camera(const CameraDrawItem& a_camera);

        /// @brief Camera 描画視点だけを空にする。
        void clear_cameras() noexcept;

        /// @brief CPU batching や queue 分類で使う StaticMesh 描画単位。
        [[nodiscard]] const std::vector<StaticMeshDrawObject>& static_mesh_objects() const noexcept;
        /// @brief ViewProjectionBuffer に upload する camera 候補。
        [[nodiscard]] const std::vector<CameraDrawItem>& cameras() const noexcept;
        /// @brief RenderableInfoBuffer に upload する連続データ。
        [[nodiscard]] const std::vector<GpuData::RenderableInfo>& renderable_infos() const noexcept;
        /// @brief TransformBuffer に upload する連続データ。
        [[nodiscard]] const std::vector<GpuData::ObjectTransformGpu>& transforms() const noexcept;

    private:
        // CPU batching や queue 分類で使う StaticMesh 描画単位
        std::vector<StaticMeshDrawObject> m_staticMeshObjects{};
        // CameraSystem が収集した camera 候補
        std::vector<CameraDrawItem> m_cameras{};
        // RenderableInfoBuffer へ upload する連続データ
        std::vector<GpuData::RenderableInfo> m_renderableInfos{};
        // TransformBuffer へ upload する連続データ
        std::vector<GpuData::ObjectTransformGpu> m_transforms{};
    };
} // namespace Cue::DrawSystem
