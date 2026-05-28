// UiLayoutSystem の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "../Components.h"
#include "../GameCoreTypes.h"
#include <DrawSystem/DrawFrameState.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

namespace Cue::ECS
{
    class UiLayoutSystem final
        : public ECSManager::System<CanvasComponent, UiRectTransformComponent>
    {
    public:
        explicit UiLayoutSystem(const DrawSystem::DrawFrameState& a_drawFrameState)
            : ECSManager::System<CanvasComponent, UiRectTransformComponent>(
                  [this](Entity a_entity,
                      CanvasComponent& a_canvas,
                      UiRectTransformComponent& a_rect,
                      const UpdateContext& a_context)
                  {
                      resolve_canvas(a_entity, a_canvas, a_rect, a_context);
                  }),
            m_drawFrameState(a_drawFrameState)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            reset_rects();
            ECSManager::System<CanvasComponent, UiRectTransformComponent>::
                update(a_context);
        }

    private:
        struct Rect final
        {
            Math::float2 min = Math::float2(0.0f, 0.0f);
            Math::float2 size = Math::float2(0.0f, 0.0f);
        };

        [[nodiscard]] Rect root_rect(
            const CanvasComponent& a_canvas,
            const UpdateContext& a_context) const noexcept
        {
            float width = a_canvas.referenceSize.x * a_canvas.scaleFactor;
            float height = a_canvas.referenceSize.y * a_canvas.scaleFactor;
            if (a_canvas.matchesScreen &&
                a_context.bufferIndex < m_drawFrameState.frameStates.size())
            {
                const DrawSystem::DrawFrameData& frameState =
                    m_drawFrameState.frame_state(a_context.bufferIndex);
                width = static_cast<float>(frameState.renderWidth);
                height = static_cast<float>(frameState.renderHeight);
            }

            Rect rect{};
            rect.size = Math::float2(
                (std::max)(width, 0.0f),
                (std::max)(height, 0.0f));
            return rect;
        }

        static void assign_rect(
            UiRectTransformComponent& a_rect,
            const Rect& a_value) noexcept
        {
            a_rect.resolvedMin = a_value.min;
            a_rect.resolvedSize = a_value.size;
            a_rect.isResolved = true;
        }

        [[nodiscard]] static Rect resolve_rect(
            const Rect& a_parent,
            const UiRectTransformComponent& a_rect) noexcept
        {
            const Math::float2 anchorMin(
                a_parent.min.x + a_parent.size.x * a_rect.anchorMin.x,
                a_parent.min.y + a_parent.size.y * a_rect.anchorMin.y);
            const Math::float2 anchorMax(
                a_parent.min.x + a_parent.size.x * a_rect.anchorMax.x,
                a_parent.min.y + a_parent.size.y * a_rect.anchorMax.y);
            const Math::float2 anchorSize(
                anchorMax.x - anchorMin.x,
                anchorMax.y - anchorMin.y);
            const Math::float2 size(
                (std::max)(anchorSize.x + a_rect.sizeDelta.x, 0.0f),
                (std::max)(anchorSize.y + a_rect.sizeDelta.y, 0.0f));
            const Math::float2 pivotPosition(
                anchorMin.x + anchorSize.x * a_rect.pivot.x +
                    a_rect.anchoredPosition.x,
                anchorMin.y + anchorSize.y * a_rect.pivot.y +
                    a_rect.anchoredPosition.y);

            Rect resolved{};
            resolved.min = Math::float2(
                pivotPosition.x - size.x * a_rect.pivot.x,
                pivotPosition.y - size.y * a_rect.pivot.y);
            resolved.size = size;
            return resolved;
        }

        void reset_rects() noexcept
        {
            auto* rectPool =
                this->m_pEcs->get_component_pool<UiRectTransformComponent>();
            if (rectPool == nullptr)
            {
                return;
            }

            for (auto& [_, components] : rectPool->map())
            {
                for (UiRectTransformComponent& rect : components)
                {
                    rect.isResolved = false;
                }
            }
        }

        void resolve_canvas(
            Entity a_entity,
            CanvasComponent& a_canvas,
            UiRectTransformComponent& a_rect,
            const UpdateContext& a_context)
        {
            const Rect rect = root_rect(a_canvas, a_context);
            assign_rect(a_rect, rect);
            resolve_children(a_entity, rect);
        }

        void resolve_children(Entity a_parent, const Rect& a_parentRect)
        {
            std::vector<Entity> children{};
            collect_children(a_parent, children);
            apply_layout_group(a_parent, a_parentRect, children);

            for (Entity child : children)
            {
                UiRectTransformComponent* childRect =
                    this->m_pEcs->get_component<UiRectTransformComponent>(child);
                if (childRect == nullptr)
                {
                    continue;
                }

                Rect resolved{};
                if (childRect->isResolved)
                {
                    resolved.min = childRect->resolvedMin;
                    resolved.size = childRect->resolvedSize;
                }
                else
                {
                    resolved = resolve_rect(a_parentRect, *childRect);
                    assign_rect(*childRect, resolved);
                }
                resolve_children(child, resolved);
            }
        }

        void collect_children(Entity a_parent, std::vector<Entity>& a_outChildren)
        {
            auto* basePool =
                this->m_pEcs->get_component_pool<GameCore::BaseComponent>();
            if (basePool == nullptr)
            {
                return;
            }

            for (auto& [entity, components] : basePool->map())
            {
                for (const GameCore::BaseComponent& base : components)
                {
                    if (base.parent == a_parent &&
                        this->m_pEcs->get_component<UiRectTransformComponent>(
                            entity) != nullptr)
                    {
                        a_outChildren.push_back(entity);
                    }
                }
            }

            std::sort(a_outChildren.begin(), a_outChildren.end());
        }

        void apply_layout_group(
            Entity a_parent,
            const Rect& a_parentRect,
            const std::vector<Entity>& a_children)
        {
            UiLayoutGroupComponent* layout =
                this->m_pEcs->get_component<UiLayoutGroupComponent>(a_parent);
            if (layout == nullptr || a_children.empty())
            {
                return;
            }

            const float left = layout->padding.x;
            const float top = layout->padding.y;
            const float right = layout->padding.z;
            const float bottom = layout->padding.w;
            const float contentWidth =
                (std::max)(a_parentRect.size.x - left - right, 0.0f);
            const float contentHeight =
                (std::max)(a_parentRect.size.y - top - bottom, 0.0f);
            const float totalSpacing =
                layout->spacing *
                static_cast<float>(a_children.size() > 1u
                    ? a_children.size() - 1u
                    : 0u);
            const bool isHorizontal =
                layout->direction == UiLayoutDirection::Horizontal;
            const float controlledMainSize =
                ((isHorizontal ? contentWidth : contentHeight) -
                    totalSpacing) /
                static_cast<float>(a_children.size());

            float cursor = isHorizontal
                ? a_parentRect.min.x + left
                : a_parentRect.min.y + top;
            for (Entity child : a_children)
            {
                UiRectTransformComponent* childRect =
                    this->m_pEcs->get_component<UiRectTransformComponent>(child);
                if (childRect == nullptr)
                {
                    continue;
                }

                Rect resolved{};
                resolved.min = Math::float2(
                    isHorizontal ? cursor : a_parentRect.min.x + left,
                    isHorizontal ? a_parentRect.min.y + top : cursor);
                resolved.size = layout->controlsChildSize
                    ? Math::float2(
                        isHorizontal
                            ? (std::max)(controlledMainSize, 0.0f)
                            : contentWidth,
                        isHorizontal
                            ? contentHeight
                            : (std::max)(controlledMainSize, 0.0f))
                    : resolve_rect(a_parentRect, *childRect).size;
                assign_rect(*childRect, resolved);

                cursor +=
                    (isHorizontal ? resolved.size.x : resolved.size.y) +
                    layout->spacing;
            }
        }

        const DrawSystem::DrawFrameState& m_drawFrameState;
    };
}
