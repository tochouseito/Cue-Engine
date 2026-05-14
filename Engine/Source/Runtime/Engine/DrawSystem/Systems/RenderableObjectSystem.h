#pragma once

// === Base includes ===
#include <CueAssert.h>

// === DrawSystem includes ===
#include <DrawSystem/DrawCollector.h>
#include <DrawSystem/DrawScene.h>
#include <DrawSystem/StaticMeshPoolTypes.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>
#include <DrawSystem/DrawFrameState.h>
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
            AssetManager* a_assetManager,
            DrawSystem::IStaticMeshPool* a_staticMeshPool,
            MaterialHandle a_defaultMaterialHandle,
            const DrawSystem::DrawFrameState& a_drawFrameState,
            DrawSystem::DrawScene& a_drawScene)
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
            m_assetManager(a_assetManager),
            m_staticMeshPool(a_staticMeshPool),
            m_defaultMaterialHandle(a_defaultMaterialHandle),
            m_drawFrameState(a_drawFrameState),
            m_drawScene(a_drawScene)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            reset_renderable_infos();
            DrawSystem::DrawCollector collector(m_drawScene, a_context.bufferIndex);
            m_currentCollector = &collector;
            begin_collect(a_context);
            ECSManager::System<RenderableInfoComponent,
                TransformComponent,
                MeshFilterComponent,
                StaticMeshRendererComponent>::update(a_context);
            m_currentCollector = nullptr;
        }

    private:
        void begin_collect(const UpdateContext& a_context)
        {
            m_currentFrameState = nullptr;
            m_renderableObjectCount = 0;
            m_isCpuBatchingEnabled = false;

            if (a_context.bufferIndex < m_drawFrameState.frameStates.size())
            {
                m_currentFrameState =
                    &m_drawFrameState.frame_state(a_context.bufferIndex);
                m_isCpuBatchingEnabled = m_currentFrameState->useCpuBatching;
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

            if (m_currentCollector == nullptr)
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
            GpuData::ObjectTransformGpu gpuTransform{};
            gpuTransform.worldMatrix = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);
            gpuTransform.normalMatrix =
                Math::float4x4::transpose(
                    Math::float4x4::inverse(gpuTransform.worldMatrix));
            GpuData::MaterialGpu gpuMaterial{};
            bool hasMaterial = false;
            if (materialHandle.valid() && m_assetManager != nullptr)
            {
                MaterialDesc materialDesc{};
                if (m_assetManager->get_material(materialHandle, materialDesc))
                {
                    gpuMaterial.color = materialDesc.color;
                    gpuMaterial.textureId = materialDesc.textureId;
                    gpuMaterial.useTexture =
                        materialDesc.isTextureUsed ? 1u : 0u;
                    gpuMaterial.useReflectionSkybox =
                        materialDesc.usesReflectionSkybox ? 1u : 0u;
                    gpuMaterial.shininess = materialDesc.shininess;
                    hasMaterial = true;
                }
            }

            GpuData::RenderObject renderObject{};
            renderObject.objectId = a_renderableInfo.objectId;
            renderObject.meshId = a_meshFilter.meshId;
            renderObject.transformId = a_renderableInfo.transformId;
            renderObject.materialId = materialId;

            DrawSystem::StaticMeshDrawItem drawItem{};
            drawItem.visibility.renderableInfo = gpuRenderableInfo;
            drawItem.visibility.renderObject = renderObject;
            drawItem.surface.transform = gpuTransform;
            drawItem.surface.material = gpuMaterial;
            drawItem.surface.hasMaterial = hasMaterial;

            if (m_isCpuBatchingEnabled &&
                m_currentFrameState != nullptr &&
                m_staticMeshPool != nullptr)
            {
                DrawSystem::StaticMeshRange meshRange{};
                if (m_staticMeshPool->get_mesh_range(
                    a_meshFilter.meshId, meshRange))
                {
                    drawItem.batching.cpuIndexedDraw = DrawSystem::CpuIndexedDraw{
                        a_renderableInfo.objectId,
                        meshRange.indexCount,
                        meshRange.startIndex,
                        meshRange.baseVertex };
                    drawItem.batching.hasCpuIndexedDraw = true;
                }
            }

            m_currentCollector->submit_static_mesh(drawItem);
            ++m_renderableObjectCount;
        }

    private:
        AssetManager* m_assetManager = nullptr;
        DrawSystem::IStaticMeshPool* m_staticMeshPool = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        const DrawSystem::DrawFrameState& m_drawFrameState;
        DrawSystem::DrawScene& m_drawScene;
        DrawSystem::DrawCollector* m_currentCollector = nullptr;
        const DrawSystem::DrawFrameData* m_currentFrameState = nullptr;
        uint32_t m_renderableObjectCount = 0;
        bool m_isCpuBatchingEnabled = false;
    };
} // namespace Cue::ECS
