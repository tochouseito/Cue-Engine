#pragma once

/// ************************************************************************************
/// GameWorld から DrawSystem 用描画リストを抽出する境界
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <cstdint>

namespace Cue::DrawSystem
{
    class DrawScene;
    enum class DrawRenderQueue : uint8_t;
    enum class DrawShadowCasterMode : uint8_t;
} // namespace Cue::DrawSystem

namespace Cue::ECS
{
    struct MeshFilterComponent;
    enum class RenderQueue : uint8_t;
    enum class ShadowCasterMode : uint8_t;
    struct StaticMeshRendererComponent;
    struct WorldTransformComponent;
} // namespace Cue::ECS

namespace Cue::GpuData
{
    struct ObjectTransformGpu;
    struct RenderableInfo;
} // namespace Cue::GpuData

namespace Cue::GameCore
{
    class GameWorld;

    /// @brief GameWorld の描画可能 Entity を DrawSystem 入力へ変換する。
    ///
    /// DrawSystem は ECS を直接知らず、この抽出器を境界として StaticMesh の描画リストだけを受け取る。
    class GameWorldRenderExtractor final
    {
    public:
        GameWorldRenderExtractor() = delete;

        /// @brief GameWorld 内の StaticMesh 描画対象を DrawScene に抽出する。
        ///
        /// MeshFilterComponent、StaticMeshRendererComponent、WorldTransformComponent を持つ Entity だけを対象にする。
        [[nodiscard]] static Result extract_static_mesh_draw_scene(GameWorld& a_world,
                                                                   DrawSystem::DrawScene& a_outScene);

    private:
        /// @brief GameCore の描画キュー指定を DrawSystem の分類へ変換する。
        [[nodiscard]] static DrawSystem::DrawRenderQueue map_render_queue(ECS::RenderQueue a_queue) noexcept;

        /// @brief GameCore の shadow caster 指定を DrawSystem の分類へ変換する。
        [[nodiscard]] static DrawSystem::DrawShadowCasterMode map_shadow_caster_mode(
            ECS::ShadowCasterMode a_mode) noexcept;

        /// @brief RenderableInfoBuffer へ upload する GPU 参照情報を構築する。
        [[nodiscard]] static GpuData::RenderableInfo make_renderable_info(
            EntityId a_entityId, uint32_t a_objectId, uint32_t a_transformId,
            const ECS::MeshFilterComponent& a_meshFilter, const ECS::StaticMeshRendererComponent& a_renderer,
            const ECS::WorldTransformComponent& a_worldTransform, bool a_isActive) noexcept;

        /// @brief TransformBuffer へ upload する world/normal matrix を構築する。
        [[nodiscard]] static GpuData::ObjectTransformGpu make_object_transform(
            const ECS::WorldTransformComponent& a_worldTransform) noexcept;
    };
} // namespace Cue::GameCore
