#pragma once

// === Base includes ===
#include <CueAssert.h>

// === RHI includes ===
#include <RHI.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>
#include <GameCore/RenderSceneState.h>
#include <GpuData/Sprite.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

namespace Cue::ECS
{
    class SpriteSystem final
        : public ECSManager::System<TransformComponent, SpriteRendererComponent>
    {
    public:
        explicit SpriteSystem(
            std::vector<RHI::SlotUploader<GpuData::SpriteInstanceGpu>>&
                a_spriteInstanceUploaders,
            AssetManager* a_assetManager,
            MaterialHandle a_defaultMaterialHandle,
            RenderSceneState& a_renderSceneState)
            : ECSManager::System<TransformComponent, SpriteRendererComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      SpriteRendererComponent& a_renderer,
                      const UpdateContext& a_context)
                  {
                      collect_sprite(
                          a_entity, a_transform, a_renderer, a_context);
                  }),
            m_spriteInstanceUploaders(a_spriteInstanceUploaders),
            m_assetManager(a_assetManager),
            m_defaultMaterialHandle(a_defaultMaterialHandle),
            m_renderSceneState(a_renderSceneState)
        {}

        void update(const UpdateContext& a_context) override
        {
            begin_uploaders(a_context);
            ECSManager::System<TransformComponent, SpriteRendererComponent>::update(
                a_context);
            upload_sprites();
            commit_uploaders();
        }

    private:
        struct SpriteUploadRecord final
        {
            GpuData::SpriteInstanceGpu instance{};
            int32_t layer = 0;
            uint32_t order = 0;
            Entity entity = 0;
        };

        void begin_uploaders(const UpdateContext& a_context)
        {
            m_currentSpriteInstanceUploader = nullptr;
            m_currentFrameState = nullptr;
            m_sprites.clear();

            if (a_context.bufferIndex < m_renderSceneState.frameStates.size())
            {
                m_currentFrameState =
                    &m_renderSceneState.frame_state(a_context.bufferIndex);
                m_currentFrameState->spriteCount = 0;
            }

            if (!m_spriteInstanceUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_spriteInstanceUploaders.size() == 1)
                    ? 0u
                    : a_context.bufferIndex;
                if (uploaderIndex < m_spriteInstanceUploaders.size())
                {
                    m_currentSpriteInstanceUploader =
                        &m_spriteInstanceUploaders[uploaderIndex];
                    m_currentSpriteInstanceUploader->begin_frame();
                }
            }
        }

        void collect_sprite(Entity a_entity,
            TransformComponent& a_transform,
            SpriteRendererComponent& a_renderer,
            const UpdateContext& a_context)
        {
            a_context;

            if (m_currentFrameState == nullptr || !a_renderer.isVisible)
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
            if (materialHandle.valid() && m_assetManager != nullptr)
            {
                MaterialDesc materialDesc{};
                if (m_assetManager->get_material(materialHandle, materialDesc))
                {
                    materialColor = materialDesc.color;
                    textureId = materialDesc.textureId;
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
            instance.rotation = a_transform.rotation.z;
            instance.pivot = a_renderer.pivot;

            SpriteUploadRecord record{};
            record.instance = instance;
            record.layer = a_renderer.layer;
            record.order = a_renderer.order;
            record.entity = a_entity;
            m_sprites.push_back(record);
        }

        void upload_sprites()
        {
            if (m_currentSpriteInstanceUploader == nullptr ||
                m_currentFrameState == nullptr)
            {
                return;
            }

            std::stable_sort(
                m_sprites.begin(),
                m_sprites.end(),
                [](const SpriteUploadRecord& a_left,
                    const SpriteUploadRecord& a_right)
                {
                    if (a_left.layer != a_right.layer)
                    {
                        return a_left.layer < a_right.layer;
                    }
                    if (a_left.order != a_right.order)
                    {
                        return a_left.order < a_right.order;
                    }
                    return a_left.entity < a_right.entity;
                });

            uint32_t spriteCount = 0;
            for (const SpriteUploadRecord& sprite : m_sprites)
            {
                if (!m_currentSpriteInstanceUploader->push(
                    spriteCount, sprite.instance))
                {
                    CUE_ASSERTF(false,
                        "Failed to queue sprite instance upload. spriteIndex=%u",
                        spriteCount);
                    break;
                }

                ++spriteCount;
            }

            m_currentFrameState->spriteCount = spriteCount;
        }

        void commit_uploaders()
        {
            if (m_currentSpriteInstanceUploader != nullptr &&
                !m_currentSpriteInstanceUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit sprite instance uploads.");
            }
        }

        std::vector<RHI::SlotUploader<GpuData::SpriteInstanceGpu>>&
            m_spriteInstanceUploaders;
        AssetManager* m_assetManager = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        RenderSceneState& m_renderSceneState;
        RHI::SlotUploader<GpuData::SpriteInstanceGpu>*
            m_currentSpriteInstanceUploader = nullptr;
        RenderFrameState* m_currentFrameState = nullptr;
        std::vector<SpriteUploadRecord> m_sprites{};
    };
} // namespace Cue::ECS
