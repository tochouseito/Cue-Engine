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

            if (!a_renderer.visible)
            {
                return;
            }

            ModelHandle modelHandle{};
            const std::vector<ModelRenderPartRecord>* renderParts = nullptr;
            const bool hasModel =
                !a_meshFilter.modelName.empty() &&
                m_assetManager != nullptr &&
                m_assetManager->get_model(
                    a_meshFilter.modelName,
                    modelHandle) &&
                m_assetManager->get_model_render_parts(
                    modelHandle,
                    renderParts) &&
                renderParts != nullptr &&
                !renderParts->empty();

            const uint32_t baseObjectId = m_renderableObjectCount;
            if (hasModel)
            {
                a_renderableInfo.objectId = baseObjectId;
                a_renderableInfo.transformId = baseObjectId;

                const Math::float4x4 entityWorld = Math::make_affine_matrix(
                    a_transform.scale,
                    a_transform.rotation,
                    a_transform.position);
                for (const ModelRenderPartRecord& renderPart : *renderParts)
                {
                    submit_static_mesh_part(
                        baseObjectId,
                        renderPart.meshId,
                        renderPart.materialIndex,
                        renderPart.localTransform * entityWorld,
                        modelHandle,
                        a_renderer);
                }
                return;
            }

            if (a_meshFilter.meshId == k_invalidMeshId)
            {
                return;
            }

            a_renderableInfo.objectId = baseObjectId;
            a_renderableInfo.transformId = baseObjectId;
            submit_static_mesh_part(
                baseObjectId,
                a_meshFilter.meshId,
                Core::Native::k_invalidModelMaterialIndex,
                Math::make_affine_matrix(
                    a_transform.scale,
                    a_transform.rotation,
                    a_transform.position),
                modelHandle,
                a_renderer);
        }

        [[nodiscard]] MaterialHandle resolve_material_handle(
            const StaticMeshRendererComponent& a_renderer) const noexcept
        {
            return a_renderer.materialHandle.valid()
                ? a_renderer.materialHandle
                : m_defaultMaterialHandle;
        }

        [[nodiscard]] bool resolve_material(
            ModelHandle a_modelHandle,
            uint32_t a_materialIndex,
            const StaticMeshRendererComponent& a_renderer,
            GpuData::MaterialGpu& outMaterial) const
        {
            MaterialDesc materialDesc{};
            bool hasMaterialDesc = false;

            const MaterialHandle materialHandle =
                a_renderer.materialHandle.valid()
                    ? a_renderer.materialHandle
                    : MaterialHandle{};
            if (materialHandle.valid() && m_assetManager != nullptr)
            {
                hasMaterialDesc =
                    static_cast<bool>(
                        m_assetManager->get_material(
                            materialHandle,
                            materialDesc));
            }

            if (!hasMaterialDesc &&
                a_modelHandle.valid() &&
                a_materialIndex != Core::Native::k_invalidModelMaterialIndex &&
                m_assetManager != nullptr)
            {
                hasMaterialDesc =
                    static_cast<bool>(
                        m_assetManager->get_model_imported_material(
                            a_modelHandle,
                            a_materialIndex,
                            materialDesc));
            }

            if (!hasMaterialDesc)
            {
                const MaterialHandle defaultMaterialHandle =
                    resolve_material_handle(a_renderer);
                if (defaultMaterialHandle.valid() && m_assetManager != nullptr)
                {
                    hasMaterialDesc =
                        static_cast<bool>(
                            m_assetManager->get_material(
                                defaultMaterialHandle,
                                materialDesc));
                }
            }

            if (!hasMaterialDesc)
            {
                return false;
            }

            outMaterial.color = materialDesc.color;
            outMaterial.textureId = materialDesc.textureId;
            outMaterial.useTexture = materialDesc.isTextureUsed ? 1u : 0u;
            outMaterial.useReflectionSkybox =
                materialDesc.usesReflectionSkybox ? 1u : 0u;
            outMaterial.shininess = materialDesc.shininess;
            return true;
        }

        void submit_static_mesh_part(
            uint32_t a_pickObjectId,
            uint32_t a_meshId,
            uint32_t a_materialIndex,
            const Math::float4x4& a_worldMatrix,
            ModelHandle a_modelHandle,
            const StaticMeshRendererComponent& a_renderer)
        {
            const uint32_t drawObjectIndex = m_renderableObjectCount;

            GpuData::RenderableInfo gpuRenderableInfo{};
            gpuRenderableInfo.objectId = a_pickObjectId;
            gpuRenderableInfo.visible = a_renderer.visible ? 1u : 0u;
            gpuRenderableInfo.meshId = a_meshId;
            gpuRenderableInfo.transformId = drawObjectIndex;
            gpuRenderableInfo.materialId = drawObjectIndex;
            gpuRenderableInfo.castsShadow = a_renderer.castsShadow ? 1u : 0u;
            gpuRenderableInfo.receivesShadow =
                a_renderer.receivesShadow ? 1u : 0u;
            GpuData::ObjectTransformGpu gpuTransform{};
            gpuTransform.worldMatrix = a_worldMatrix;
            gpuTransform.normalMatrix =
                Math::float4x4::transpose(
                    Math::float4x4::inverse(gpuTransform.worldMatrix));
            GpuData::MaterialGpu gpuMaterial{};
            const bool hasMaterial = resolve_material(
                a_modelHandle,
                a_materialIndex,
                a_renderer,
                gpuMaterial);

            GpuData::RenderObject renderObject{};
            renderObject.objectId = a_pickObjectId;
            renderObject.meshId = a_meshId;
            renderObject.transformId = drawObjectIndex;
            renderObject.materialId = drawObjectIndex;
            renderObject.castsShadow = a_renderer.castsShadow ? 1u : 0u;
            renderObject.receivesShadow = a_renderer.receivesShadow ? 1u : 0u;

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
                    a_meshId, meshRange))
                {
                    drawItem.batching.cpuIndexedDraw = DrawSystem::CpuIndexedDraw{
                        drawObjectIndex,
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
