#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <DrawSystem/DrawCollector.h>
#include <DrawSystem/DrawFrameState.h>
#include <DrawSystem/DrawScene.h>
#include <DrawSystem/FontAtlasManager.h>
#include <GameCore/Components.h>
#include <GpuData/Sprite.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

namespace Cue::ECS
{
    class TextSystem final
        : public ECSManager::System<WorldTransformComponent, TextRendererComponent>
    {
    public:
        TextSystem(
            DrawSystem::FontAtlasManager& a_fontAtlasManager,
            DrawSystem::DrawFrameState& a_drawFrameState,
            DrawSystem::DrawScene& a_drawScene)
            : ECSManager::System<WorldTransformComponent, TextRendererComponent>(
                  [this](Entity a_entity,
                      WorldTransformComponent& a_transform,
                      TextRendererComponent& a_text,
                      const UpdateContext& a_context)
                  {
                      collect_text(a_entity, a_transform, a_text, a_context);
                  }),
            m_fontAtlasManager(a_fontAtlasManager),
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
            ECSManager::System<WorldTransformComponent, TextRendererComponent>::
                update(a_context);
            m_currentCollector = nullptr;
            m_currentFrameState = nullptr;
        }

    private:
        struct TextRect final
        {
            Math::float2 min = Math::float2(0.0f, 0.0f);
            Math::float2 size = Math::float2(0.0f, 0.0f);
        };

        [[nodiscard]] static float horizontal_offset(
            TextHorizontalAlign a_align,
            float a_width,
            float a_lineWidth) noexcept
        {
            switch (a_align)
            {
            case TextHorizontalAlign::Center:
                return (std::max)((a_width - a_lineWidth) * 0.5f, 0.0f);
            case TextHorizontalAlign::Right:
                return (std::max)(a_width - a_lineWidth, 0.0f);
            case TextHorizontalAlign::Left:
            default:
                return 0.0f;
            }
        }

        [[nodiscard]] static float vertical_offset(
            TextVerticalAlign a_align,
            float a_height,
            float a_textHeight) noexcept
        {
            switch (a_align)
            {
            case TextVerticalAlign::Middle:
                return (std::max)((a_height - a_textHeight) * 0.5f, 0.0f);
            case TextVerticalAlign::Bottom:
                return (std::max)(a_height - a_textHeight, 0.0f);
            case TextVerticalAlign::Top:
            default:
                return 0.0f;
            }
        }

        [[nodiscard]] TextRect resolve_rect(
            Entity a_entity,
            const WorldTransformComponent& a_transform,
            float a_fallbackWidth,
            float a_fallbackHeight) const
        {
            TextRect rect{};
            const UiRectTransformComponent* rectTransform =
                this->m_pEcs->get_component<UiRectTransformComponent>(a_entity);
            if (rectTransform != nullptr && rectTransform->isResolved)
            {
                rect.min = rectTransform->resolvedMin;
                rect.size = rectTransform->resolvedSize;
                return rect;
            }

            rect.min = Math::float2(a_transform.position.x, a_transform.position.y);
            rect.size = Math::float2(a_fallbackWidth, a_fallbackHeight);
            return rect;
        }

        void collect_text(Entity a_entity,
            const WorldTransformComponent& a_transform,
            TextRendererComponent& a_text,
            const UpdateContext& a_context)
        {
            a_context;
            if (m_currentCollector == nullptr ||
                m_currentFrameState == nullptr ||
                !a_text.visible ||
                a_text.text.empty())
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

            const std::string fontPath = a_text.fontPath.empty()
                ? DrawSystem::FontAtlasManager::default_font_path()
                : a_text.fontPath;
            const uint32_t fontSize = (std::max)(a_text.fontSize, 1u);
            Result result =
                m_fontAtlasManager.ensure_text(fontPath, fontSize, a_text.text);
            if (!result)
            {
                return;
            }

            const DrawSystem::FontAtlasView atlas =
                m_fontAtlasManager.atlas_view(fontPath, fontSize);
            if (atlas.textureId == AssetManager::k_errorTextureId)
            {
                return;
            }

            std::vector<uint32_t> codepoints{};
            DrawSystem::FontAtlasManager::decode_utf8(a_text.text, codepoints);

            std::vector<float> lineWidths{};
            lineWidths.push_back(0.0f);
            for (uint32_t codepoint : codepoints)
            {
                if (codepoint == '\r')
                {
                    continue;
                }
                if (codepoint == '\n')
                {
                    lineWidths.push_back(0.0f);
                    continue;
                }

                const DrawSystem::FontGlyph* glyph =
                    m_fontAtlasManager.find_glyph(fontPath, fontSize, codepoint);
                if (glyph != nullptr)
                {
                    lineWidths.back() += glyph->advance;
                }
            }

            float maxLineWidth = 0.0f;
            for (float width : lineWidths)
            {
                maxLineWidth = (std::max)(maxLineWidth, width);
            }

            const float textHeight =
                atlas.lineHeight * static_cast<float>(lineWidths.size());
            const TextRect rect =
                resolve_rect(a_entity, a_transform, maxLineWidth, textHeight);
            float penX = horizontal_offset(
                a_text.horizontalAlign, rect.size.x, lineWidths.front());
            float penY = vertical_offset(
                a_text.verticalAlign, rect.size.y, textHeight);
            size_t lineIndex = 0;

            for (uint32_t codepoint : codepoints)
            {
                if (codepoint == '\r')
                {
                    continue;
                }
                if (codepoint == '\n')
                {
                    ++lineIndex;
                    penX = horizontal_offset(
                        a_text.horizontalAlign,
                        rect.size.x,
                        lineIndex < lineWidths.size() ? lineWidths[lineIndex] : 0.0f);
                    penY += atlas.lineHeight;
                    continue;
                }

                const DrawSystem::FontGlyph* glyph =
                    m_fontAtlasManager.find_glyph(fontPath, fontSize, codepoint);
                if (glyph == nullptr)
                {
                    continue;
                }

                if (glyph->size.x > 0.0f && glyph->size.y > 0.0f)
                {
                    const float screenX = rect.min.x + penX + glyph->bearing.x;
                    const float screenY =
                        rect.min.y + penY + atlas.ascender - glyph->bearing.y;

                    GpuData::SpriteInstanceGpu instance{};
                    instance.positionSize = Math::float4(
                        (screenX / renderWidth) * 2.0f - 1.0f,
                        1.0f - (screenY / renderHeight) * 2.0f,
                        (glyph->size.x / renderWidth) * 2.0f,
                        (glyph->size.y / renderHeight) * 2.0f);
                    instance.uvRect = glyph->uvRect;
                    instance.color = a_text.color;
                    instance.textureId = atlas.textureId;
                    instance.useTexture = 1u;
                    instance.rotation = 0.0f;
                    instance.pivot = Math::float2(0.0f, 0.0f);

                    DrawSystem::SpriteDrawItem item{};
                    item.instance = instance;
                    item.layer = a_text.layer;
                    item.order = a_text.order;
                    item.entity = a_entity;
                    m_currentCollector->submit_sprite(item);
                }

                penX += glyph->advance;
            }
        }

        DrawSystem::FontAtlasManager& m_fontAtlasManager;
        DrawSystem::DrawFrameState& m_drawFrameState;
        DrawSystem::DrawScene& m_drawScene;
        DrawSystem::DrawCollector* m_currentCollector = nullptr;
        const DrawSystem::DrawFrameData* m_currentFrameState = nullptr;
    };
}
