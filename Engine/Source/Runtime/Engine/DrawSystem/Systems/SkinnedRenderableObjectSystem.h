// SkinnedRenderableObjectSystem の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <CueAssert.h>

// === DrawSystem includes ===
#include <DrawSystem/DrawCollector.h>
#include <DrawSystem/DrawFrameState.h>
#include <DrawSystem/DrawScene.h>
#include <DrawSystem/StaticMeshPoolTypes.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>
#include <GpuData/Batching.h>
#include <GpuData/Transform.h>

namespace Cue::ECS
{
    class SkinnedRenderableObjectSystem final
        : public ECSManager::System<RenderableInfoComponent,
              WorldTransformComponent,
              MeshFilterComponent,
              SkinnedMeshRendererComponent>
    {
    public:
        explicit SkinnedRenderableObjectSystem(
            AssetManager* a_assetManager,
            DrawSystem::IStaticMeshPool* a_staticMeshPool,
            MaterialHandle a_defaultMaterialHandle,
            const DrawSystem::DrawFrameState& a_drawFrameState,
            DrawSystem::DrawScene& a_drawScene)
            : ECSManager::System<RenderableInfoComponent,
                  WorldTransformComponent,
                  MeshFilterComponent,
                  SkinnedMeshRendererComponent>(
                  [this](Entity a_entity,
                      RenderableInfoComponent& a_renderableInfo,
                      WorldTransformComponent& a_transform,
                      MeshFilterComponent& a_meshFilter,
                      SkinnedMeshRendererComponent& a_renderer,
                      const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_renderableInfo,
                          a_transform, a_meshFilter, a_renderer, a_context);
                  })
            , m_assetManager(a_assetManager)
            , m_staticMeshPool(a_staticMeshPool)
            , m_defaultMaterialHandle(a_defaultMaterialHandle)
            , m_drawFrameState(a_drawFrameState)
            , m_drawScene(a_drawScene)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            DrawSystem::DrawCollector collector(m_drawScene, a_context.bufferIndex);
            m_currentCollector = &collector;
            begin_collect(a_context);
            ECSManager::System<RenderableInfoComponent,
                WorldTransformComponent,
                MeshFilterComponent,
                SkinnedMeshRendererComponent>::update(a_context);
            m_currentCollector = nullptr;
        }

    private:
        void begin_collect(const UpdateContext& a_context)
        {
            m_currentFrameState = nullptr;
            m_renderableObjectCount = 0;
            m_skinPaletteCount = 0;
            m_isCpuBatchingEnabled = false;

            if (a_context.bufferIndex < m_drawScene.frame_count())
            {
                const DrawSystem::DrawSceneFrame& frame =
                    m_drawScene.frame(a_context.bufferIndex);
                m_renderableObjectCount =
                    static_cast<uint32_t>(frame.staticMeshSurfaceItems.size());
                for (const DrawSystem::StaticMeshSurfaceItem& item :
                     frame.staticMeshSurfaceItems)
                {
                    m_skinPaletteCount +=
                        static_cast<uint32_t>(item.skinPalette.size());
                }
            }

            if (a_context.bufferIndex < m_drawFrameState.frameStates.size())
            {
                m_currentFrameState =
                    &m_drawFrameState.frame_state(a_context.bufferIndex);
                m_isCpuBatchingEnabled = m_currentFrameState->useCpuBatching;
            }
        }

        void update_component(Entity a_entity,
            RenderableInfoComponent& a_renderableInfo,
            WorldTransformComponent& a_transform,
            MeshFilterComponent& a_meshFilter,
            SkinnedMeshRendererComponent& a_renderer,
            const UpdateContext& a_context)
        {
            a_entity;
            a_context;

            if (m_currentCollector == nullptr || !a_renderer.visible)
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
            if (!hasModel)
            {
                return;
            }

            const AnimationComponent* animation =
                this->m_pEcs->get_component<AnimationComponent>(a_entity);
            const std::vector<Math::float4x4>* skinPalette =
                animation != nullptr && !animation->skinPalette.empty()
                ? &animation->skinPalette
                : nullptr;

            StaticMeshRendererComponent rendererProxy{};
            rendererProxy.materialHandle = a_renderer.materialHandle;
            rendererProxy.propertyBlock = a_renderer.propertyBlock;
            rendererProxy.renderQueue = a_renderer.renderQueue;
            rendererProxy.shadowCasterMode = a_renderer.shadowCasterMode;
            rendererProxy.visible = a_renderer.visible;
            rendererProxy.castsShadow = a_renderer.castsShadow;
            rendererProxy.receivesShadow = a_renderer.receivesShadow;

            const uint32_t baseObjectId = m_renderableObjectCount;
            a_renderableInfo.objectId = baseObjectId;
            a_renderableInfo.transformId = baseObjectId;

            const Math::float4x4 entityWorld = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);
            for (const ModelRenderPartRecord& renderPart : *renderParts)
            {
                bool hasSkinInfluence = false;
                if (m_staticMeshPool != nullptr)
                {
                    Result skinResult = m_staticMeshPool->has_skin_influence(
                        renderPart.meshId,
                        hasSkinInfluence);
                    if (!skinResult)
                    {
                        hasSkinInfluence = false;
                    }
                }

                const Math::float4x4& localTransform =
                    resolve_part_transform(renderPart, animation);
                const Math::float4x4 worldMatrix = hasSkinInfluence
                    ? entityWorld
                    : localTransform * entityWorld;
                submit_mesh_part(baseObjectId,
                    renderPart.meshId,
                    renderPart.materialIndex,
                    worldMatrix,
                    modelHandle,
                    rendererProxy,
                    hasSkinInfluence ? skinPalette : nullptr);
            }
        }

        [[nodiscard]] static const Math::float4x4& resolve_part_transform(
            const ModelRenderPartRecord& a_renderPart,
            const AnimationComponent* a_animation) noexcept
        {
            if (a_animation != nullptr &&
                a_renderPart.jointIndex <
                    static_cast<uint32_t>(a_animation->modelPose.size()))
            {
                return a_animation->modelPose[a_renderPart.jointIndex];
            }

            return a_renderPart.localTransform;
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
            apply_property_block(a_renderer.propertyBlock, outMaterial);
            return true;
        }

        static void apply_property_block(
            const MaterialPropertyBlock& a_propertyBlock,
            GpuData::MaterialGpu& a_material) noexcept
        {
            if ((a_propertyBlock.overrideMask &
                    MaterialPropertyOverrideColor) != 0u)
            {
                a_material.color = a_propertyBlock.color;
            }
            if ((a_propertyBlock.overrideMask &
                    MaterialPropertyOverrideShininess) != 0u)
            {
                a_material.shininess = a_propertyBlock.shininess;
            }
            if ((a_propertyBlock.overrideMask &
                    MaterialPropertyOverrideReflectionSkybox) != 0u)
            {
                a_material.useReflectionSkybox =
                    a_propertyBlock.usesReflectionSkybox ? 1u : 0u;
            }
        }

        [[nodiscard]] static DrawSystem::StaticMeshRenderQueue resolve_render_queue(
            RenderQueue a_renderQueue,
            const GpuData::MaterialGpu& a_material) noexcept
        {
            switch (a_renderQueue)
            {
            case RenderQueue::Opaque:
                return DrawSystem::StaticMeshRenderQueue::Opaque;
            case RenderQueue::Transparent:
                return DrawSystem::StaticMeshRenderQueue::Transparent;
            case RenderQueue::Auto:
            default:
                break;
            }

            return a_material.color.a < 1.0f
                ? DrawSystem::StaticMeshRenderQueue::Transparent
                : DrawSystem::StaticMeshRenderQueue::Opaque;
        }

        void submit_mesh_part(
            uint32_t a_pickObjectId,
            uint32_t a_meshId,
            uint32_t a_materialIndex,
            const Math::float4x4& a_worldMatrix,
            ModelHandle a_modelHandle,
            const StaticMeshRendererComponent& a_renderer,
            const std::vector<Math::float4x4>* a_skinPalette)
        {
            const uint32_t drawObjectIndex = m_renderableObjectCount;
            const uint32_t skinPaletteOffset =
                a_skinPalette != nullptr && !a_skinPalette->empty()
                ? m_skinPaletteCount
                : UINT32_MAX;
            const uint32_t skinPaletteCount =
                a_skinPalette != nullptr
                ? static_cast<uint32_t>(a_skinPalette->size())
                : 0u;

            GpuData::RenderableInfo gpuRenderableInfo{};
            gpuRenderableInfo.objectId = a_pickObjectId;
            gpuRenderableInfo.visible = a_renderer.visible ? 1u : 0u;
            gpuRenderableInfo.meshId = a_meshId;
            gpuRenderableInfo.transformId = drawObjectIndex;
            gpuRenderableInfo.materialId = drawObjectIndex;
            gpuRenderableInfo.castsShadow = a_renderer.castsShadow ? 1u : 0u;
            gpuRenderableInfo.receivesShadow =
                a_renderer.receivesShadow ? 1u : 0u;
            gpuRenderableInfo.shadowCasterMode =
                static_cast<uint32_t>(a_renderer.shadowCasterMode);
            gpuRenderableInfo.skinPaletteOffset = skinPaletteOffset;
            gpuRenderableInfo.skinPaletteCount = skinPaletteCount;

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
            renderObject.shadowCasterMode =
                static_cast<uint32_t>(a_renderer.shadowCasterMode);
            renderObject.skinPaletteOffset = skinPaletteOffset;
            renderObject.skinPaletteCount = skinPaletteCount;

            DrawSystem::StaticMeshDrawItem drawItem{};
            drawItem.visibility.renderableInfo = gpuRenderableInfo;
            drawItem.visibility.renderObject = renderObject;
            drawItem.surface.transform = gpuTransform;
            drawItem.surface.material = gpuMaterial;
            drawItem.surface.hasMaterial = hasMaterial;
            if (a_skinPalette != nullptr)
            {
                drawItem.surface.skinPalette.reserve(a_skinPalette->size());
                for (const Math::float4x4& matrix : *a_skinPalette)
                {
                    GpuData::SkinPaletteGpu palette{};
                    palette.matrix = matrix;
                    drawItem.surface.skinPalette.push_back(palette);
                }
                m_skinPaletteCount += skinPaletteCount;
            }

            if (m_staticMeshPool != nullptr)
            {
                DrawSystem::StaticMeshRange meshRange{};
                if (m_staticMeshPool->get_mesh_range(a_meshId, meshRange))
                {
                    drawItem.batching.cpuIndexedDraw =
                        DrawSystem::CpuIndexedDraw{ drawObjectIndex,
                            meshRange.indexCount,
                            meshRange.startIndex,
                            meshRange.baseVertex,
                            0.0f };
                    drawItem.batching.hasCpuIndexedDraw = true;
                }
            }

            drawItem.surface.renderQueue =
                resolve_render_queue(a_renderer.renderQueue, gpuMaterial);

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
        uint32_t m_skinPaletteCount = 0;
        bool m_isCpuBatchingEnabled = false;
    };
} // namespace Cue::ECS
