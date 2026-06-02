// SpriteSystem の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <CueAssert.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <DrawSystem/DrawCollector.h>
#include <DrawSystem/DrawScene.h>
#include <GameCore/Components.h>
#include <DrawSystem/DrawFrameState.h>
#include <GpuData/Sprite.h>

namespace Cue::ECS
{
    class SpriteSystem final
        : public ECSManager::System<WorldTransformComponent, SpriteRendererComponent>
    {
    public:
        explicit SpriteSystem(
            AssetManager* a_assetManager,
            MaterialHandle a_defaultMaterialHandle,
            DrawSystem::DrawFrameState& a_drawFrameState,
            DrawSystem::DrawScene& a_drawScene)
            : ECSManager::System<WorldTransformComponent, SpriteRendererComponent>(
                  [this](Entity a_entity,
                      WorldTransformComponent& a_transform,
                      SpriteRendererComponent& a_renderer,
                      const UpdateContext& a_context)
                  {
                      collect_sprite(
                          a_entity, a_transform, a_renderer, a_context);
                  }),
            m_assetManager(a_assetManager),
            m_defaultMaterialHandle(a_defaultMaterialHandle),
            m_drawFrameState(a_drawFrameState),
            m_drawScene(a_drawScene)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentCollector = nullptr;
            m_currentFrameState = nullptr;
            if (a_context.bufferIndex < m_drawFrameState.frameStates.size())
            {
                m_currentFrameState =
                    &m_drawFrameState.frame_state(a_context.bufferIndex);
            }

            DrawSystem::DrawCollector collector(m_drawScene, a_context.bufferIndex);
            m_currentCollector = &collector;
            ECSManager::System<WorldTransformComponent, SpriteRendererComponent>::update(
                a_context);
            m_currentCollector = nullptr;
            m_currentFrameState = nullptr;
        }

    private:

        void collect_sprite(Entity a_entity,
            WorldTransformComponent& a_transform,
            SpriteRendererComponent& a_renderer,
            const UpdateContext& a_context)
        {
            a_context;

            if (m_currentCollector == nullptr ||
                m_currentFrameState == nullptr ||
                !a_renderer.isVisible)
            {
                return;
            }

            const float renderWidth =
                static_cast<float>(m_currentFrameState->renderWidth);
            const float renderHeight =
                static_cast<float>(m_currentFrameState->renderHeight);
            if (renderWidth <= 0.0f || renderHeight <= 0.0f)
            {
                return;
            }

            const MaterialHandle materialHandle =
                a_renderer.materialHandle.valid()
                ? a_renderer.materialHandle
                : m_defaultMaterialHandle;

            Math::float4 materialColor(1.0f, 1.0f, 1.0f, 1.0f);
            uint32_t textureId = 0;
            uint32_t useTexture = 0;
            if (materialHandle.valid() && m_assetManager != nullptr)
            {
                MaterialDesc materialDesc{};
                if (m_assetManager->get_material(materialHandle, materialDesc))
                {
                    materialColor = materialDesc.color;
                    textureId = materialDesc.textureId;
                    useTexture = materialDesc.isTextureUsed ? 1u : 0u;
                }
            }

            const float sizeX = a_renderer.size.x * a_transform.scale.x;
            const float sizeY = a_renderer.size.y * a_transform.scale.y;
            const float positionX =
                (a_transform.position.x / renderWidth) * 2.0f - 1.0f;
            const float positionY =
                1.0f - (a_transform.position.y / renderHeight) * 2.0f;
            const float sizeNdcX = (sizeX / renderWidth) * 2.0f;
            const float sizeNdcY = (sizeY / renderHeight) * 2.0f;

            GpuData::SpriteInstanceGpu instance{};
            instance.positionSize =
                Math::float4(positionX, positionY, sizeNdcX, sizeNdcY);
            instance.uvRect = a_renderer.uvRect;
            instance.color = Math::float4(
                a_renderer.color.r * materialColor.r,
                a_renderer.color.g * materialColor.g,
                a_renderer.color.b * materialColor.b,
                a_renderer.color.a * materialColor.a);
            instance.textureId = textureId;
            instance.useTexture = useTexture;
            instance.rotation =
                Math::quaternion_to_euler_xyz(a_transform.rotation).z;
            instance.pivot = a_renderer.pivot;

            DrawSystem::SpriteDrawItem drawItem{};
            drawItem.instance = instance;
            drawItem.layer = a_renderer.layer;
            drawItem.order = a_renderer.order;
            drawItem.entity = a_entity;
            m_currentCollector->submit_sprite(drawItem);
        }

        AssetManager* m_assetManager = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        DrawSystem::DrawFrameState& m_drawFrameState;
        DrawSystem::DrawScene& m_drawScene;
        DrawSystem::DrawCollector* m_currentCollector = nullptr;
        const DrawSystem::DrawFrameData* m_currentFrameState = nullptr;
    };
} // namespace Cue::ECS
