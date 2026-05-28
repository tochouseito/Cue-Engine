// UiWidgetSystem の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "../Components.h"
#include <Asset/AssetManager.h>
#include <DrawSystem/DrawScene.h>
#include <DrawSystem/DrawFrameState.h>
#include <GpuData/Sprite.h>

// === PAL includes ===
#include <Input/InputManager.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

namespace Cue::ECS
{
    class UiWidgetSystem final : public ECSManager::ISystem
    {
    public:
        explicit UiWidgetSystem(
            PAL::InputManager* a_inputManager,
            AssetManager* a_assetManager,
            MaterialHandle a_defaultMaterialHandle,
            DrawSystem::DrawFrameState& a_drawFrameState,
            DrawSystem::DrawScene& a_drawScene)
            : m_inputManager(a_inputManager),
            m_assetManager(a_assetManager),
            m_defaultMaterialHandle(a_defaultMaterialHandle),
            m_drawFrameState(a_drawFrameState),
            m_drawScene(a_drawScene)
        {
        }

        void update() override
        {
            update(UpdateContext{});
        }

        void update(const UpdateContext& a_context) override
        {
            if (m_pEcs == nullptr ||
                a_context.bufferIndex >= m_drawFrameState.frameStates.size())
            {
                return;
            }

            const DrawSystem::DrawFrameData& frameState =
                m_drawFrameState.frame_state(a_context.bufferIndex);
            if (frameState.renderWidth == 0u || frameState.renderHeight == 0u)
            {
                return;
            }

            reset_runtime_state();
            process_input();
            collect_draw_items(a_context.bufferIndex, frameState);
        }

    private:
        enum class WidgetKind : uint8_t
        {
            Button,
            Checkbox,
            Slider,
        };

        struct HitCandidate final
        {
            Entity entity = 0;
            WidgetKind kind = WidgetKind::Button;
            int32_t layer = 0;
            uint32_t order = 0;
        };

        [[nodiscard]] static bool contains(
            const UiRectTransformComponent& a_rect,
            const Math::float2& a_position) noexcept
        {
            return a_rect.isResolved &&
                a_position.x >= a_rect.resolvedMin.x &&
                a_position.y >= a_rect.resolvedMin.y &&
                a_position.x <= a_rect.resolvedMin.x + a_rect.resolvedSize.x &&
                a_position.y <= a_rect.resolvedMin.y + a_rect.resolvedSize.y;
        }

        [[nodiscard]] bool is_entity_live(Entity a_entity) const noexcept
        {
            return m_pEcs != nullptr && m_pEcs->is_entity_active(a_entity);
        }

        [[nodiscard]] Math::float4 material_color(
            MaterialHandle a_materialHandle,
            uint32_t& a_outTextureId,
            uint32_t& a_outUseTexture) const
        {
            a_outTextureId = 0;
            a_outUseTexture = 0;
            Math::float4 color(1.0f, 1.0f, 1.0f, 1.0f);
            const MaterialHandle materialHandle = a_materialHandle.valid()
                ? a_materialHandle
                : m_defaultMaterialHandle;
            if (materialHandle.valid() && m_assetManager != nullptr)
            {
                MaterialDesc materialDesc{};
                if (m_assetManager->get_material(materialHandle, materialDesc))
                {
                    color = materialDesc.color;
                    a_outTextureId = materialDesc.textureId;
                    a_outUseTexture = materialDesc.isTextureUsed ? 1u : 0u;
                }
            }
            return color;
        }

        void reset_runtime_state() noexcept
        {
            auto* buttonPool = m_pEcs->get_component_pool<UiButtonComponent>();
            if (buttonPool != nullptr)
            {
                for (auto& [entity, components] : buttonPool->map())
                {
                    for (UiButtonComponent& button : components)
                    {
                        button.isHovered = false;
                        button.wasClicked = false;
                        button.hasFocus = entity == m_focusedEntity;
                        if (!button.isInteractable)
                        {
                            button.isPressed = false;
                        }
                    }
                }
            }

            auto* checkboxPool =
                m_pEcs->get_component_pool<UiCheckboxComponent>();
            if (checkboxPool != nullptr)
            {
                for (auto& [entity, components] : checkboxPool->map())
                {
                    for (UiCheckboxComponent& checkbox : components)
                    {
                        checkbox.isHovered = false;
                        checkbox.wasChanged = false;
                        checkbox.hasFocus = entity == m_focusedEntity;
                        if (!checkbox.isInteractable)
                        {
                            checkbox.isPressed = false;
                        }
                    }
                }
            }

            auto* sliderPool = m_pEcs->get_component_pool<UiSliderComponent>();
            if (sliderPool != nullptr)
            {
                for (auto& [entity, components] : sliderPool->map())
                {
                    for (UiSliderComponent& slider : components)
                    {
                        slider.isHovered = false;
                        slider.wasChanged = false;
                        slider.hasFocus = entity == m_focusedEntity;
                        if (!slider.isInteractable)
                        {
                            slider.isDragging = false;
                        }
                    }
                }
            }

            if (m_focusedEntity != k_invalidFocusEntity &&
                !is_entity_live(m_focusedEntity))
            {
                m_focusedEntity = k_invalidFocusEntity;
            }
        }

        void process_input()
        {
            if (m_inputManager == nullptr)
            {
                return;
            }

            const PAL::MousePosition mousePosition =
                m_inputManager->mouse_position();
            const Math::float2 pointer(
                static_cast<float>(mousePosition.x),
                static_cast<float>(mousePosition.y));
            const bool isLeftDown =
                m_inputManager->push_mouse_button(PAL::MouseButton::Left);
            const bool wasLeftPressed =
                m_inputManager->mouse_button_pressed(PAL::MouseButton::Left);
            const bool wasLeftReleased =
                m_inputManager->mouse_button_released(PAL::MouseButton::Left);

            std::vector<HitCandidate> candidates{};
            collect_hit_candidates(pointer, candidates);
            std::sort(candidates.begin(), candidates.end(),
                [](const HitCandidate& a_left, const HitCandidate& a_right)
                {
                    if (a_left.layer != a_right.layer)
                    {
                        return a_left.layer > a_right.layer;
                    }
                    if (a_left.order != a_right.order)
                    {
                        return a_left.order > a_right.order;
                    }
                    return a_left.entity > a_right.entity;
                });

            const HitCandidate* top = candidates.empty() ? nullptr : &candidates.front();
            if (wasLeftPressed)
            {
                m_focusedEntity =
                    top != nullptr ? top->entity : k_invalidFocusEntity;
            }

            process_buttons(top, isLeftDown, wasLeftPressed, wasLeftReleased);
            process_checkboxes(top, isLeftDown, wasLeftPressed, wasLeftReleased);
            process_sliders(pointer, top, isLeftDown, wasLeftPressed, wasLeftReleased);
        }

        void collect_hit_candidates(
            const Math::float2& a_pointer,
            std::vector<HitCandidate>& a_outCandidates)
        {
            auto* rectPool = m_pEcs->get_component_pool<UiRectTransformComponent>();
            if (rectPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : rectPool->map())
            {
                if (!is_entity_live(entity) || components.empty())
                {
                    continue;
                }

                const UiRectTransformComponent& rect = components.front();
                if (!rect.is_active() || !contains(rect, a_pointer))
                {
                    continue;
                }

                if (const UiButtonComponent* button =
                        m_pEcs->get_component<UiButtonComponent>(entity);
                    button != nullptr && button->is_active() &&
                    button->isInteractable)
                {
                    a_outCandidates.push_back(
                        { entity, WidgetKind::Button, button->layer, button->order });
                }

                if (const UiCheckboxComponent* checkbox =
                        m_pEcs->get_component<UiCheckboxComponent>(entity);
                    checkbox != nullptr && checkbox->is_active() &&
                    checkbox->isInteractable)
                {
                    a_outCandidates.push_back(
                        { entity, WidgetKind::Checkbox, checkbox->layer,
                            checkbox->order });
                }

                if (const UiSliderComponent* slider =
                        m_pEcs->get_component<UiSliderComponent>(entity);
                    slider != nullptr && slider->is_active() &&
                    slider->isInteractable)
                {
                    a_outCandidates.push_back(
                        { entity, WidgetKind::Slider, slider->layer, slider->order });
                }
            }
        }

        void process_buttons(
            const HitCandidate* a_top,
            bool a_isLeftDown,
            bool a_wasLeftPressed,
            bool a_wasLeftReleased) noexcept
        {
            auto* buttonPool = m_pEcs->get_component_pool<UiButtonComponent>();
            if (buttonPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : buttonPool->map())
            {
                const bool isTop =
                    a_top != nullptr && a_top->entity == entity &&
                    a_top->kind == WidgetKind::Button;
                for (UiButtonComponent& button : components)
                {
                    if (!button.is_active() || !button.isInteractable)
                    {
                        continue;
                    }

                    button.isHovered = isTop;
                    if (isTop && a_wasLeftPressed)
                    {
                        button.isPressed = true;
                    }
                    if (button.isPressed && a_wasLeftReleased)
                    {
                        button.wasClicked = isTop;
                        button.isPressed = false;
                    }
                    if (!a_isLeftDown)
                    {
                        button.isPressed = false;
                    }
                    button.hasFocus = entity == m_focusedEntity;
                }
            }
        }

        void process_checkboxes(
            const HitCandidate* a_top,
            bool a_isLeftDown,
            bool a_wasLeftPressed,
            bool a_wasLeftReleased) noexcept
        {
            auto* checkboxPool =
                m_pEcs->get_component_pool<UiCheckboxComponent>();
            if (checkboxPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : checkboxPool->map())
            {
                const bool isTop =
                    a_top != nullptr && a_top->entity == entity &&
                    a_top->kind == WidgetKind::Checkbox;
                for (UiCheckboxComponent& checkbox : components)
                {
                    if (!checkbox.is_active() || !checkbox.isInteractable)
                    {
                        continue;
                    }

                    checkbox.isHovered = isTop;
                    if (isTop && a_wasLeftPressed)
                    {
                        checkbox.isPressed = true;
                    }
                    if (checkbox.isPressed && a_wasLeftReleased)
                    {
                        if (isTop)
                        {
                            checkbox.isChecked = !checkbox.isChecked;
                            checkbox.wasChanged = true;
                        }
                        checkbox.isPressed = false;
                    }
                    if (!a_isLeftDown)
                    {
                        checkbox.isPressed = false;
                    }
                    checkbox.hasFocus = entity == m_focusedEntity;
                }
            }
        }

        void process_sliders(
            const Math::float2& a_pointer,
            const HitCandidate* a_top,
            bool a_isLeftDown,
            bool a_wasLeftPressed,
            bool a_wasLeftReleased) noexcept
        {
            auto* sliderPool = m_pEcs->get_component_pool<UiSliderComponent>();
            if (sliderPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : sliderPool->map())
            {
                const bool isTop =
                    a_top != nullptr && a_top->entity == entity &&
                    a_top->kind == WidgetKind::Slider;
                for (UiSliderComponent& slider : components)
                {
                    if (!slider.is_active() || !slider.isInteractable)
                    {
                        continue;
                    }

                    slider.isHovered = isTop;
                    if (isTop && a_wasLeftPressed)
                    {
                        slider.isDragging = true;
                    }
                    if (a_wasLeftReleased)
                    {
                        slider.isDragging = false;
                    }
                    if (slider.isDragging && a_isLeftDown)
                    {
                        set_slider_value_from_pointer(entity, slider, a_pointer);
                    }
                    slider.hasFocus = entity == m_focusedEntity;
                }
            }
        }

        void set_slider_value_from_pointer(
            Entity a_entity,
            UiSliderComponent& a_slider,
            const Math::float2& a_pointer) noexcept
        {
            const UiRectTransformComponent* rect =
                m_pEcs->get_component<UiRectTransformComponent>(a_entity);
            if (rect == nullptr || !rect->isResolved || rect->resolvedSize.x <= 0.0f)
            {
                return;
            }

            const float range = a_slider.maxValue - a_slider.minValue;
            if (range <= 0.0f)
            {
                a_slider.value = a_slider.minValue;
                return;
            }

            const float normalized = std::clamp(
                (a_pointer.x - rect->resolvedMin.x) / rect->resolvedSize.x,
                0.0f,
                1.0f);
            const float nextValue = a_slider.minValue + range * normalized;
            if (nextValue != a_slider.value)
            {
                a_slider.value = nextValue;
                a_slider.wasChanged = true;
            }
        }

        void collect_draw_items(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState)
        {
            collect_images(a_bufferIndex, a_frameState);
            collect_button_backgrounds(a_bufferIndex, a_frameState);
            collect_checkbox_backgrounds(a_bufferIndex, a_frameState);
            collect_slider_backgrounds(a_bufferIndex, a_frameState);
        }

        void collect_images(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState)
        {
            auto* imagePool = m_pEcs->get_component_pool<UiImageComponent>();
            if (imagePool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : imagePool->map())
            {
                if (!is_entity_live(entity))
                {
                    continue;
                }

                const UiRectTransformComponent* rect =
                    m_pEcs->get_component<UiRectTransformComponent>(entity);
                if (rect == nullptr || !rect->isResolved)
                {
                    continue;
                }

                for (const UiImageComponent& image : components)
                {
                    if (!image.is_active() || !image.visible)
                    {
                        continue;
                    }

                    uint32_t textureId = 0;
                    uint32_t useTexture = 0;
                    const Math::float4 materialColor =
                        material_color(image.materialHandle, textureId, useTexture);
                    const Math::float4 color(
                        image.color.r * materialColor.r,
                        image.color.g * materialColor.g,
                        image.color.b * materialColor.b,
                        image.color.a * materialColor.a);
                    submit_rect_sprite(a_bufferIndex,
                        a_frameState,
                        entity,
                        *rect,
                        image.uvRect,
                        color,
                        textureId,
                        useTexture,
                        image.layer,
                        image.order,
                        rect->pivot);
                }
            }
        }

        void collect_button_backgrounds(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState)
        {
            auto* buttonPool = m_pEcs->get_component_pool<UiButtonComponent>();
            if (buttonPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : buttonPool->map())
            {
                if (!is_entity_live(entity))
                {
                    continue;
                }

                const UiRectTransformComponent* rect =
                    m_pEcs->get_component<UiRectTransformComponent>(entity);
                if (rect == nullptr || !rect->isResolved)
                {
                    continue;
                }

                for (const UiButtonComponent& button : components)
                {
                    if (!button.is_active())
                    {
                        continue;
                    }

                    const Math::float4 color = !button.isInteractable
                        ? button.disabledColor
                        : button.isPressed ? button.pressedColor
                                           : button.isHovered ? button.hoverColor
                                                              : button.normalColor;
                    submit_solid_rect(a_bufferIndex,
                        a_frameState,
                        entity,
                        *rect,
                        color,
                        button.layer,
                        button.order);
                }
            }
        }

        void collect_checkbox_backgrounds(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState)
        {
            auto* checkboxPool =
                m_pEcs->get_component_pool<UiCheckboxComponent>();
            if (checkboxPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : checkboxPool->map())
            {
                if (!is_entity_live(entity))
                {
                    continue;
                }

                const UiRectTransformComponent* rect =
                    m_pEcs->get_component<UiRectTransformComponent>(entity);
                if (rect == nullptr || !rect->isResolved)
                {
                    continue;
                }

                for (const UiCheckboxComponent& checkbox : components)
                {
                    if (!checkbox.is_active())
                    {
                        continue;
                    }

                    const Math::float4 color = !checkbox.isInteractable
                        ? checkbox.disabledColor
                        : checkbox.isHovered ? checkbox.hoverColor
                                             : checkbox.normalColor;
                    submit_solid_rect(a_bufferIndex,
                        a_frameState,
                        entity,
                        *rect,
                        color,
                        checkbox.layer,
                        checkbox.order);

                    if (checkbox.isChecked)
                    {
                        UiRectTransformComponent checkRect = *rect;
                        const float inset =
                            (std::min)(rect->resolvedSize.x, rect->resolvedSize.y) *
                            0.25f;
                        checkRect.resolvedMin = Math::float2(
                            rect->resolvedMin.x + inset,
                            rect->resolvedMin.y + inset);
                        checkRect.resolvedSize = Math::float2(
                            (std::max)(rect->resolvedSize.x - inset * 2.0f, 0.0f),
                            (std::max)(rect->resolvedSize.y - inset * 2.0f, 0.0f));
                        submit_solid_rect(a_bufferIndex,
                            a_frameState,
                            entity,
                            checkRect,
                            checkbox.checkColor,
                            checkbox.layer,
                            checkbox.order + 1u);
                    }
                }
            }
        }

        void collect_slider_backgrounds(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState)
        {
            auto* sliderPool = m_pEcs->get_component_pool<UiSliderComponent>();
            if (sliderPool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : sliderPool->map())
            {
                if (!is_entity_live(entity))
                {
                    continue;
                }

                const UiRectTransformComponent* rect =
                    m_pEcs->get_component<UiRectTransformComponent>(entity);
                if (rect == nullptr || !rect->isResolved)
                {
                    continue;
                }

                for (const UiSliderComponent& slider : components)
                {
                    if (!slider.is_active())
                    {
                        continue;
                    }

                    const float range = slider.maxValue - slider.minValue;
                    const float normalized = range > 0.0f
                        ? std::clamp((slider.value - slider.minValue) / range,
                              0.0f,
                              1.0f)
                        : 0.0f;
                    const Math::float4 trackColor = slider.isInteractable
                        ? slider.trackColor
                        : slider.disabledColor;

                    UiRectTransformComponent trackRect = *rect;
                    trackRect.resolvedMin.y =
                        rect->resolvedMin.y + rect->resolvedSize.y * 0.35f;
                    trackRect.resolvedSize.y = rect->resolvedSize.y * 0.3f;
                    submit_solid_rect(a_bufferIndex,
                        a_frameState,
                        entity,
                        trackRect,
                        trackColor,
                        slider.layer,
                        slider.order);

                    UiRectTransformComponent fillRect = trackRect;
                    fillRect.resolvedSize.x = trackRect.resolvedSize.x * normalized;
                    submit_solid_rect(a_bufferIndex,
                        a_frameState,
                        entity,
                        fillRect,
                        slider.fillColor,
                        slider.layer,
                        slider.order + 1u);

                    UiRectTransformComponent handleRect = *rect;
                    const float handleWidth =
                        (std::max)(8.0f, rect->resolvedSize.y * 0.45f);
                    handleRect.resolvedSize.x = handleWidth;
                    handleRect.resolvedMin.x =
                        rect->resolvedMin.x +
                        rect->resolvedSize.x * normalized -
                        handleWidth * 0.5f;
                    submit_solid_rect(a_bufferIndex,
                        a_frameState,
                        entity,
                        handleRect,
                        slider.handleColor,
                        slider.layer,
                        slider.order + 2u);
                }
            }
        }

        void submit_solid_rect(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState,
            Entity a_entity,
            const UiRectTransformComponent& a_rect,
            const Math::float4& a_color,
            int32_t a_layer,
            uint32_t a_order)
        {
            submit_rect_sprite(a_bufferIndex,
                a_frameState,
                a_entity,
                a_rect,
                Math::float4(0.0f, 0.0f, 1.0f, 1.0f),
                a_color,
                0u,
                0u,
                a_layer,
                a_order,
                a_rect.pivot);
        }

        void submit_rect_sprite(
            uint32_t a_bufferIndex,
            const DrawSystem::DrawFrameData& a_frameState,
            Entity a_entity,
            const UiRectTransformComponent& a_rect,
            const Math::float4& a_uvRect,
            const Math::float4& a_color,
            uint32_t a_textureId,
            uint32_t a_useTexture,
            int32_t a_layer,
            uint32_t a_order,
            const Math::float2& a_pivot)
        {
            const float renderWidth = static_cast<float>(a_frameState.renderWidth);
            const float renderHeight = static_cast<float>(a_frameState.renderHeight);
            const float screenX = a_rect.resolvedMin.x + a_rect.resolvedSize.x *
                a_pivot.x;
            const float screenY = a_rect.resolvedMin.y + a_rect.resolvedSize.y *
                a_pivot.y;

            GpuData::SpriteInstanceGpu instance{};
            instance.positionSize = Math::float4(
                (screenX / renderWidth) * 2.0f - 1.0f,
                1.0f - (screenY / renderHeight) * 2.0f,
                (a_rect.resolvedSize.x / renderWidth) * 2.0f,
                (a_rect.resolvedSize.y / renderHeight) * 2.0f);
            instance.uvRect = a_uvRect;
            instance.color = a_color;
            instance.textureId = a_textureId;
            instance.useTexture = a_useTexture;
            instance.rotation = 0.0f;
            instance.pivot = a_pivot;

            DrawSystem::SpriteDrawItem drawItem{};
            drawItem.instance = instance;
            drawItem.layer = a_layer;
            drawItem.order = a_order;
            drawItem.entity = a_entity;
            m_drawScene.submit_sprite(a_bufferIndex, drawItem);
        }

        static constexpr Entity k_invalidFocusEntity =
            static_cast<Entity>(-1);

        PAL::InputManager* m_inputManager = nullptr;
        AssetManager* m_assetManager = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        DrawSystem::DrawFrameState& m_drawFrameState;
        DrawSystem::DrawScene& m_drawScene;
        Entity m_focusedEntity = k_invalidFocusEntity;
    };
}
