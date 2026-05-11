#pragma once

// === Base includes ===
#include <CueAssert.h>

// === RHI includes ===
#include <RHI.h>

// === DrawSystem includes ===
#include <DrawSystem/StaticMeshPoolTypes.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
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
            std::vector<RHI::SlotUploader<GpuData::MaterialGpu>>&
                a_materialUploaders,
            std::vector<RHI::SlotUploader<GpuData::RenderObject>>&
                a_renderObjectUploaders,
            std::vector<RHI::SlotUploader<uint32_t>>&
                a_visibleObjectCountUploaders,
            AssetManager* a_assetManager,
            DrawSystem::IStaticMeshPool* a_staticMeshPool,
            MaterialHandle a_defaultMaterialHandle,
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
            m_materialUploaders(a_materialUploaders),
            m_renderObjectUploaders(a_renderObjectUploaders),
            m_visibleObjectCountUploaders(a_visibleObjectCountUploaders),
            m_assetManager(a_assetManager),
            m_staticMeshPool(a_staticMeshPool),
            m_defaultMaterialHandle(a_defaultMaterialHandle),
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
            m_currentMaterialUploader = nullptr;
            m_currentRenderObjectUploader = nullptr;
            m_currentVisibleObjectCountUploader = nullptr;
            m_currentFrameState = nullptr;
            m_renderableObjectCount = 0;
            m_isCpuBatchingEnabled = false;

            if (a_context.bufferIndex < m_renderSceneState.frameStates.size())
            {
                m_currentFrameState =
                    &m_renderSceneState.frame_state(a_context.bufferIndex);
                m_isCpuBatchingEnabled = m_currentFrameState->useCpuBatching;
                m_currentFrameState->cpuIndexedDraws.clear();
            }

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

            if (!m_renderObjectUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_renderObjectUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_renderObjectUploaders.size())
                {
                    m_currentRenderObjectUploader =
                        &m_renderObjectUploaders[uploaderIndex];
                    m_currentRenderObjectUploader->begin_frame();
                }
            }

            if (!m_materialUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_materialUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_materialUploaders.size())
                {
                    m_currentMaterialUploader = &m_materialUploaders[uploaderIndex];
                    m_currentMaterialUploader->begin_frame();
                }
            }

            if (!m_visibleObjectCountUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_visibleObjectCountUploaders.size() == 1)
                    ? 0u
                    : a_context.bufferIndex;
                if (uploaderIndex < m_visibleObjectCountUploaders.size())
                {
                    m_currentVisibleObjectCountUploader =
                        &m_visibleObjectCountUploaders[uploaderIndex];
                    m_currentVisibleObjectCountUploader->begin_frame();
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

            if (m_currentMaterialUploader != nullptr &&
                !m_currentMaterialUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit material uploads.");
            }

            if (m_isCpuBatchingEnabled &&
                m_currentVisibleObjectCountUploader != nullptr)
            {
                if (!m_currentVisibleObjectCountUploader->push(
                    0, m_renderableObjectCount))
                {
                    CUE_ASSERTF(false,
                        "Failed to queue visible object count upload.");
                }
            }

            if (m_currentRenderObjectUploader != nullptr &&
                !m_currentRenderObjectUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit render object uploads.");
            }

            if (m_currentVisibleObjectCountUploader != nullptr &&
                !m_currentVisibleObjectCountUploader->commit())
            {
                CUE_ASSERTF(false,
                    "Failed to commit visible object count uploads.");
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

            const MaterialHandle materialHandle =
                a_renderer.materialHandle.valid()
                ? a_renderer.materialHandle
                : m_defaultMaterialHandle;
            uint32_t materialId = 0;
            if (materialHandle.valid())
            {
                materialId = materialHandle.index;
            }

            GpuData::RenderableInfo gpuRenderableInfo{};
            gpuRenderableInfo.objectId = a_renderableInfo.objectId;
            gpuRenderableInfo.visible = a_renderer.visible ? 1u : 0u;
            gpuRenderableInfo.meshId = a_meshFilter.meshId;
            gpuRenderableInfo.transformId = a_renderableInfo.transformId;
            gpuRenderableInfo.materialId = materialId;
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
            gpuTransform.normalMatrix =
                Math::float4x4::transpose(
                    Math::float4x4::inverse(gpuTransform.worldMatrix));
            if (!m_currentTransformUploader->push(
                a_renderableInfo.transformId, gpuTransform))
            {
                CUE_ASSERTF(false,
                    "Failed to queue transform upload. transformId=%u",
                    a_renderableInfo.transformId);
                return;
            }

            if (m_currentMaterialUploader != nullptr && materialHandle.valid() &&
                m_assetManager != nullptr)
            {
                MaterialDesc materialDesc{};
                if (m_assetManager->get_material(materialHandle, materialDesc))
                {
                    GpuData::MaterialGpu gpuMaterial{};
                    gpuMaterial.color = materialDesc.color;
                    gpuMaterial.textureId = materialDesc.textureId;
                    gpuMaterial.useTexture =
                        materialDesc.isTextureUsed ? 1u : 0u;
                    if (!m_currentMaterialUploader->push(materialId, gpuMaterial))
                    {
                        CUE_ASSERTF(false,
                            "Failed to queue material upload. materialId=%u",
                            materialId);
                        return;
                    }
                }
            }

            if (m_currentRenderObjectUploader != nullptr)
            {
                GpuData::RenderObject renderObject{};
                renderObject.objectId = a_renderableInfo.objectId;
                renderObject.meshId = a_meshFilter.meshId;
                renderObject.transformId = a_renderableInfo.transformId;
                renderObject.materialId = materialId;
                if (!m_currentRenderObjectUploader->push(
                    a_renderableInfo.objectId, renderObject))
                {
                    CUE_ASSERTF(false,
                        "Failed to queue render object upload. objectId=%u",
                        a_renderableInfo.objectId);
                    return;
                }

                if (m_isCpuBatchingEnabled &&
                    m_currentFrameState != nullptr &&
                    m_staticMeshPool != nullptr)
                {
                    DrawSystem::StaticMeshRange meshRange{};
                    if (m_staticMeshPool->get_mesh_range(
                        a_meshFilter.meshId, meshRange))
                    {
                        m_currentFrameState->cpuIndexedDraws.push_back(
                            CpuIndexedDraw{
                                a_renderableInfo.objectId,
                                meshRange.indexCount,
                                meshRange.startIndex,
                                meshRange.baseVertex });
                    }
                }
            }

            ++m_renderableObjectCount;
        }

    private:
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>&
            m_renderableInfoUploaders;
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>&
            m_transformUploaders;
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>>&
            m_materialUploaders;
        std::vector<RHI::SlotUploader<GpuData::RenderObject>>&
            m_renderObjectUploaders;
        std::vector<RHI::SlotUploader<uint32_t>>&
            m_visibleObjectCountUploaders;
        AssetManager* m_assetManager = nullptr;
        DrawSystem::IStaticMeshPool* m_staticMeshPool = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        RenderSceneState& m_renderSceneState;
        RHI::SlotUploader<GpuData::RenderableInfo>*
            m_currentRenderableInfoUploader = nullptr;
        RHI::SlotUploader<GpuData::ObjectTransformGpu>*
            m_currentTransformUploader = nullptr;
        RHI::SlotUploader<GpuData::MaterialGpu>*
            m_currentMaterialUploader = nullptr;
        RHI::SlotUploader<GpuData::RenderObject>*
            m_currentRenderObjectUploader = nullptr;
        RHI::SlotUploader<uint32_t>* m_currentVisibleObjectCountUploader = nullptr;
        RenderFrameState* m_currentFrameState = nullptr;
        uint32_t m_renderableObjectCount = 0;
        bool m_isCpuBatchingEnabled = false;
    };
} // namespace Cue::ECS
