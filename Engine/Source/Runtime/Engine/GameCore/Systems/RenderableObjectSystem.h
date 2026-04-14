#pragma once

// === Base includes ===
#include <CueAssert.h>

// === RHI includes ===
#include <RHI.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/RenderSceneState.h>
#include <GpuData/Batching.h>
#include <GpuData/Transform.h>

namespace Cue::ECS
{
    class RenderableObjectSystem final
        : public ECSManager::System<RenderableInfoComponent,
              TransformComponent,
              MeshFilterComponent,
              StaticMeshRendererComponent>
    {
    public:
        explicit RenderableObjectSystem(
            std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>&
                a_renderableInfoUploaders,
            std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>&
                a_transformUploaders,
            RenderSceneState& a_renderSceneState)
            : ECSManager::System<RenderableInfoComponent,
                  TransformComponent,
                  MeshFilterComponent,
                  StaticMeshRendererComponent>(
                  [this](Entity a_entity,
                      RenderableInfoComponent& a_renderableInfo,
                      TransformComponent& a_transform,
                      MeshFilterComponent& a_meshFilter,
                      StaticMeshRendererComponent& a_renderer,
                      const UpdateContext& a_context) {
                          update_component(a_entity, a_renderableInfo,
                              a_transform, a_meshFilter, a_renderer, a_context);
                  }),
            m_renderableInfoUploaders(a_renderableInfoUploaders),
            m_transformUploaders(a_transformUploaders),
            m_renderSceneState(a_renderSceneState)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            reset_renderable_infos();
            begin_uploaders(a_context);
            ECSManager::System<RenderableInfoComponent,
                TransformComponent,
                MeshFilterComponent,
                StaticMeshRendererComponent>::update(a_context);
            commit_uploaders();

            if (a_context.bufferIndex < m_renderSceneState.frameStates.size())
            {
                RenderFrameState& frameState =
                    m_renderSceneState.frame_state(a_context.bufferIndex);
                frameState.objectCount = m_renderableObjectCount;
            }
        }

    private:
        void begin_uploaders(const UpdateContext& a_context)
        {
            m_currentRenderableInfoUploader = nullptr;
            m_currentTransformUploader = nullptr;
            m_renderableObjectCount = 0;

            if (!m_renderableInfoUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_renderableInfoUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_renderableInfoUploaders.size())
                {
                    m_currentRenderableInfoUploader =
                        &m_renderableInfoUploaders[uploaderIndex];
                    m_currentRenderableInfoUploader->begin_frame();
                }
            }

            if (!m_transformUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_transformUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_transformUploaders.size())
                {
                    m_currentTransformUploader = &m_transformUploaders[uploaderIndex];
                    m_currentTransformUploader->begin_frame();
                }
            }
        }

        void commit_uploaders()
        {
            if (m_currentRenderableInfoUploader != nullptr &&
                !m_currentRenderableInfoUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit renderable info uploads.");
            }

            if (m_currentTransformUploader != nullptr &&
                !m_currentTransformUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit transform uploads.");
            }
        }

        void reset_renderable_infos()
        {
            auto* renderableInfoPool =
                this->m_pEcs->get_component_pool<RenderableInfoComponent>();
            if (renderableInfoPool == nullptr)
            {
                return;
            }

            for (auto& [_, components] : renderableInfoPool->map())
            {
                for (RenderableInfoComponent& renderableInfo : components)
                {
                    renderableInfo.objectId = k_invalidRenderableId;
                    renderableInfo.transformId = k_invalidRenderableId;
                }
            }
        }

        void update_component(Entity a_entity,
            RenderableInfoComponent& a_renderableInfo,
            TransformComponent& a_transform,
            MeshFilterComponent& a_meshFilter,
            StaticMeshRendererComponent& a_renderer,
            const UpdateContext& a_context)
        {
            a_entity;
            a_context;

            if (m_currentRenderableInfoUploader == nullptr ||
                m_currentTransformUploader == nullptr)
            {
                return;
            }

            if (a_meshFilter.meshId == k_invalidMeshId || !a_renderer.visible)
            {
                return;
            }

            a_renderableInfo.objectId = m_renderableObjectCount;
            a_renderableInfo.transformId = m_renderableObjectCount;

            GpuData::RenderableInfo gpuRenderableInfo{};
            gpuRenderableInfo.objectId = a_renderableInfo.objectId;
            gpuRenderableInfo.visible = a_renderer.visible ? 1u : 0u;
            gpuRenderableInfo.meshId = a_meshFilter.meshId;
            gpuRenderableInfo.transformId = a_renderableInfo.transformId;
            if (!m_currentRenderableInfoUploader->push(
                a_renderableInfo.objectId, gpuRenderableInfo))
            {
                CUE_ASSERTF(false,
                    "Failed to queue renderable info upload. objectId=%u",
                    a_renderableInfo.objectId);
                return;
            }

            GpuData::ObjectTransformGpu gpuTransform{};
            gpuTransform.worldMatrix = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);
            if (!m_currentTransformUploader->push(
                a_renderableInfo.transformId, gpuTransform))
            {
                CUE_ASSERTF(false,
                    "Failed to queue transform upload. transformId=%u",
                    a_renderableInfo.transformId);
                return;
            }

            ++m_renderableObjectCount;
        }

    private:
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>&
            m_renderableInfoUploaders;
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>&
            m_transformUploaders;
        RenderSceneState& m_renderSceneState;
        RHI::SlotUploader<GpuData::RenderableInfo>*
            m_currentRenderableInfoUploader = nullptr;
        RHI::SlotUploader<GpuData::ObjectTransformGpu>*
            m_currentTransformUploader = nullptr;
        uint32_t m_renderableObjectCount = 0;
    };
} // namespace Cue::ECS
