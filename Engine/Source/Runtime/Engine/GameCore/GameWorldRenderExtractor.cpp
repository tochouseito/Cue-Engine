#include "GameWorldRenderExtractor.h"

// === Engine includes ===
#include "Components.h"
#include "GameWorld.h"

#include "DrawSystem/DrawScene.h"
#include "DrawSystem/GpuData/Batching.h"
#include "DrawSystem/GpuData/Transform.h"

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <limits>

namespace Cue::GameCore
{
    Result GameWorldRenderExtractor::extract_static_mesh_draw_scene(GameWorld& a_world,
                                                                    DrawSystem::DrawScene& a_outScene)
    {
        a_outScene.clear();

        // for_each_object は途中終了できないため、最初の失敗を保持して残りの visitor 処理を止める。
        Result extractResult = Result::ok();
        const Result eachResult = a_world.for_each_object(
            [&](EntityId a_entityId, GameObject a_object)
            {
                if (!extractResult)
                {
                    return;
                }

                bool isActive = false;
                Result result = a_object.is_active(isActive);
                if (!result)
                {
                    extractResult = result;
                    return;
                }

                ECS::MeshFilterComponent* meshFilter = nullptr;
                result = a_world.get_component<ECS::MeshFilterComponent>(a_entityId, meshFilter);
                if (result.code == Code::NotFound)
                {
                    // StaticMesh ではない Entity は DrawScene に含めない。
                    return;
                }
                if (!result)
                {
                    extractResult = result;
                    return;
                }

                ECS::StaticMeshRendererComponent* renderer = nullptr;
                result = a_world.get_component<ECS::StaticMeshRendererComponent>(a_entityId, renderer);
                if (result.code == Code::NotFound)
                {
                    // MeshFilter だけを持つ Entity は描画設定が未確定なので除外する。
                    return;
                }
                if (!result)
                {
                    extractResult = result;
                    return;
                }

                ECS::WorldTransformComponent* worldTransform = nullptr;
                result = a_world.get_component<ECS::WorldTransformComponent>(a_entityId, worldTransform);
                if (result.code == Code::NotFound)
                {
                    // TransformBuffer を作れない Entity は DrawSystem に渡さない。
                    return;
                }
                if (!result)
                {
                    extractResult = result;
                    return;
                }

                ECS::RenderableInfoComponent* renderableInfoComponent = nullptr;
                result = a_world.get_component<ECS::RenderableInfoComponent>(a_entityId, renderableInfoComponent);
                if (!result)
                {
                    extractResult = result;
                    return;
                }

                if (a_outScene.object_count() >= (std::numeric_limits<uint32_t>::max)())
                {
                    extractResult = Result::fail(Code::InvalidState, Severity::Error,
                                                 "DrawScene object count exceeds renderable ID range.");
                    return;
                }

                const uint32_t objectId = static_cast<uint32_t>(a_outScene.object_count());
                const uint32_t transformId = objectId;

                // 現段階では DrawScene 内の連続 index を GPU buffer の参照 ID として使う。
                DrawSystem::StaticMeshDrawObject drawObject{};
                drawObject.sourceEntityId = a_entityId;
                drawObject.objectId = objectId;
                drawObject.transformId = transformId;
                drawObject.meshId = meshFilter->meshId;
                drawObject.materialId = renderer->materialId;
                drawObject.renderQueue = map_render_queue(renderer->renderQueue);
                drawObject.shadowCasterMode = map_shadow_caster_mode(renderer->shadowCasterMode);
                drawObject.visible = isActive && renderer->visible;
                drawObject.castsShadow = renderer->castsShadow;
                drawObject.receivesShadow = renderer->receivesShadow;

                const GpuData::RenderableInfo renderableInfo = make_renderable_info(
                    a_entityId, objectId, transformId, *meshFilter, *renderer, *worldTransform, isActive);
                const GpuData::ObjectTransformGpu transform = make_object_transform(*worldTransform);

                extractResult = a_outScene.add_static_mesh_object(drawObject, renderableInfo, transform);
                if (!extractResult)
                {
                    return;
                }

                renderableInfoComponent->objectId = objectId;
                renderableInfoComponent->transformId = transformId;
            });

        if (!eachResult)
        {
            return eachResult;
        }

        return extractResult;
    }

    DrawSystem::DrawRenderQueue GameWorldRenderExtractor::map_render_queue(ECS::RenderQueue a_queue) noexcept
    {
        switch (a_queue)
        {
        case ECS::RenderQueue::Transparent:
            return DrawSystem::DrawRenderQueue::Transparent;
        case ECS::RenderQueue::Opaque:
        case ECS::RenderQueue::Auto:
        default:
            return DrawSystem::DrawRenderQueue::Opaque;
        }
    }

    DrawSystem::DrawShadowCasterMode GameWorldRenderExtractor::map_shadow_caster_mode(
        ECS::ShadowCasterMode a_mode) noexcept
    {
        switch (a_mode)
        {
        case ECS::ShadowCasterMode::TwoSided:
            return DrawSystem::DrawShadowCasterMode::TwoSided;
        case ECS::ShadowCasterMode::Solid:
        default:
            return DrawSystem::DrawShadowCasterMode::Solid;
        }
    }

    GpuData::RenderableInfo GameWorldRenderExtractor::make_renderable_info(
        EntityId, uint32_t a_objectId, uint32_t a_transformId, const ECS::MeshFilterComponent& a_meshFilter,
        const ECS::StaticMeshRendererComponent& a_renderer, const ECS::WorldTransformComponent& a_worldTransform,
        bool a_isActive) noexcept
    {
        GpuData::RenderableInfo info{};
        // RenderableInfo は shader から objectId/transformId を起点に Mesh/Material/Transform を参照する。
        info.objectId = a_objectId;
        info.visible = (a_isActive && a_renderer.visible) ? 1u : 0u;
        info.meshId = a_meshFilter.meshId;
        info.transformId = a_transformId;
        info.materialId = a_renderer.materialId;
        info.castsShadow = a_renderer.castsShadow ? 1u : 0u;
        info.receivesShadow = a_renderer.receivesShadow ? 1u : 0u;
        info.shadowCasterMode = static_cast<uint32_t>(map_shadow_caster_mode(a_renderer.shadowCasterMode));
        info.lodMeshId0 = a_meshFilter.meshId;
        info.lodCount = 1;
        info.occluderMeshId = a_meshFilter.meshId;
        info.boundsCenterRadius =
            Math::float4(a_worldTransform.position.x, a_worldTransform.position.y, a_worldTransform.position.z, 0.0f);
        return info;
    }

    GpuData::ObjectTransformGpu GameWorldRenderExtractor::make_object_transform(
        const ECS::WorldTransformComponent& a_worldTransform) noexcept
    {
        GpuData::ObjectTransformGpu transform{};
        transform.worldMatrix =
            Math::make_affine_matrix(a_worldTransform.scale, a_worldTransform.rotation, a_worldTransform.position);
        // normalMatrix は非一様 scale を含む場合でも法線方向を保つため inverse-transpose にする。
        transform.normalMatrix = Math::float4x4::transpose(Math::float4x4::inverse(transform.worldMatrix));
        return transform;
    }
} // namespace Cue::GameCore
