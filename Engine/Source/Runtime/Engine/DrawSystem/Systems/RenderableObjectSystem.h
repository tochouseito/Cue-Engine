#pragma once

/// ****************************************************************************
/// StaticMesh component を DrawScene へ変換する System
/// ****************************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "DrawSystem/DrawScene.h"
#include "DrawSystem/GpuData/Batching.h"
#include "DrawSystem/GpuData/Transform.h"
#include "GameCore/Components.h"

// === C++ includes ===
#include <vector>

namespace Cue::ECS
{
    class RenderableObjectSystem final
        : public ECSManager::System<RenderableInfoComponent,
                                    WorldTransformComponent,
                                    MeshFilterComponent,
                                    StaticMeshRendererComponent>
    {
    public:
        /// @brief StaticMesh entity を frame ごとの DrawScene へ収集する。
        explicit RenderableObjectSystem(std::vector<DrawSystem::DrawScene>& a_drawScenes)
            : ECSManager::System<RenderableInfoComponent,
                                 WorldTransformComponent,
                                 MeshFilterComponent,
                                 StaticMeshRendererComponent>(
                  [this](Entity a_entity,
                         RenderableInfoComponent& a_renderableInfo,
                         WorldTransformComponent& a_transform,
                         MeshFilterComponent& a_meshFilter,
                         StaticMeshRendererComponent& a_renderer,
                         const UpdateContext& a_context)
                  {
                      update_component(a_entity,
                                       a_renderableInfo,
                                       a_transform,
                                       a_meshFilter,
                                       a_renderer,
                                       a_context);
                  })
            , m_drawScenes(a_drawScenes)
        {
        }

        /// @brief 現在 frame の StaticMesh component を DrawScene へ反映する。
        void update(const UpdateContext& a_context) override
        {
            if (a_context.bufferIndex >= m_drawScenes.size())
            {
                return;
            }

            ECSManager::System<RenderableInfoComponent,
                               WorldTransformComponent,
                               MeshFilterComponent,
                               StaticMeshRendererComponent>::update(a_context);
        }

    private:
        void update_component(Entity a_entity,
                              RenderableInfoComponent& a_renderableInfo,
                              WorldTransformComponent& a_transform,
                              MeshFilterComponent& a_meshFilter,
                              StaticMeshRendererComponent& a_renderer,
                              const UpdateContext& a_context)
        {
            if (!a_renderer.visible || a_meshFilter.meshId == k_invalidMeshId)
            {
                return;
            }

            DrawSystem::DrawScene& drawScene = m_drawScenes[a_context.bufferIndex];
            const uint32_t objectId = static_cast<uint32_t>(drawScene.object_count());
            const uint32_t transformId = objectId;

            a_renderableInfo.objectId = objectId;
            a_renderableInfo.transformId = transformId;

            DrawSystem::StaticMeshDrawObject drawObject{};
            drawObject.sourceEntityId = a_entity;
            drawObject.objectId = objectId;
            drawObject.transformId = transformId;
            drawObject.meshId = a_meshFilter.meshId;
            drawObject.materialId = a_renderer.materialId;
            drawObject.renderQueue = map_render_queue(a_renderer.renderQueue);
            drawObject.shadowCasterMode = map_shadow_caster_mode(a_renderer.shadowCasterMode);
            drawObject.visible = a_renderer.visible;
            drawObject.castsShadow = a_renderer.castsShadow;
            drawObject.receivesShadow = a_renderer.receivesShadow;

            GpuData::RenderableInfo renderableInfo{};
            renderableInfo.objectId = objectId;
            renderableInfo.visible = a_renderer.visible ? 1u : 0u;
            renderableInfo.meshId = a_meshFilter.meshId;
            renderableInfo.transformId = transformId;
            renderableInfo.materialId = a_renderer.materialId;
            renderableInfo.castsShadow = a_renderer.castsShadow ? 1u : 0u;
            renderableInfo.receivesShadow = a_renderer.receivesShadow ? 1u : 0u;
            renderableInfo.shadowCasterMode =
                static_cast<uint32_t>(drawObject.shadowCasterMode);
            renderableInfo.lodMeshId0 = a_meshFilter.meshId;
            renderableInfo.lodCount = 1;
            renderableInfo.occluderMeshId = a_meshFilter.meshId;
            renderableInfo.boundsCenterRadius =
                Math::float4(a_transform.position.x, a_transform.position.y, a_transform.position.z, 0.0f);

            GpuData::ObjectTransformGpu transform{};
            transform.worldMatrix =
                Math::make_affine_matrix(a_transform.scale, a_transform.rotation, a_transform.position);
            transform.normalMatrix =
                Math::float4x4::transpose(Math::float4x4::inverse(transform.worldMatrix));

            const Result result = drawScene.add_static_mesh_object(drawObject, renderableInfo, transform);
            CUE_ASSERT_FORMAT(success(result), "Failed to add static mesh to DrawScene: {}", result.message.data());
        }

        [[nodiscard]] static DrawSystem::DrawRenderQueue map_render_queue(RenderQueue a_queue) noexcept
        {
            switch (a_queue)
            {
            case RenderQueue::Transparent:
                return DrawSystem::DrawRenderQueue::Transparent;
            case RenderQueue::Opaque:
            case RenderQueue::Auto:
            default:
                return DrawSystem::DrawRenderQueue::Opaque;
            }
        }

        [[nodiscard]] static DrawSystem::DrawShadowCasterMode map_shadow_caster_mode(
            ShadowCasterMode a_mode) noexcept
        {
            switch (a_mode)
            {
            case ShadowCasterMode::TwoSided:
                return DrawSystem::DrawShadowCasterMode::TwoSided;
            case ShadowCasterMode::Solid:
            default:
                return DrawSystem::DrawShadowCasterMode::Solid;
            }
        }

        // 出力先 DrawScene は Engine が frame resource ごとに所有する。
        std::vector<DrawSystem::DrawScene>& m_drawScenes;
    };
} // namespace Cue::ECS
