#pragma once

// === ShadowSystem includes ===
#include <ShadowSystem/ShadowCollector.h>
#include <ShadowSystem/ShadowScene.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === C++ includes ===
#include <algorithm>

namespace Cue::ECS
{
    class ShadowSystem final
        : public ECSManager::System<TransformComponent>
    {
    public:
        explicit ShadowSystem(Cue::ShadowSystem::ShadowScene& a_shadowScene)
            : ECSManager::System<TransformComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_transform, a_context);
                  })
            , m_shadowScene(a_shadowScene)
        {}

        void update(const UpdateContext& a_context) override
        {
            Cue::ShadowSystem::ShadowCollector collector(
                m_shadowScene, a_context.bufferIndex);
            m_currentCollector = &collector;
            m_spotLightIndex = 0;
            m_hasSpotShadow = false;
            ECSManager::System<TransformComponent>::update(a_context);
            m_currentCollector = nullptr;
        }

    private:
        void update_component(
            Entity a_entity,
            TransformComponent& a_transform,
            const UpdateContext& a_context)
        {
            a_entity;
            a_context;

            if (m_currentCollector == nullptr || this->m_pEcs == nullptr)
            {
                return;
            }

            SpotLightComponent* spotLight =
                this->m_pEcs->get_component<SpotLightComponent>(a_entity);
            if (spotLight == nullptr || !spotLight->isEnabled)
            {
                return;
            }

            const uint32_t currentSpotLightIndex = m_spotLightIndex;
            ++m_spotLightIndex;

            if (m_hasSpotShadow || !spotLight->castsShadow)
            {
                return;
            }

            const float outerAngle = std::clamp(
                spotLight->outerAngleDegrees,
                1.0f,
                89.0f);
            const float fovY = Math::degrees_to_radians(outerAngle * 2.0f);
            const float range = (std::max)(spotLight->range, 0.001f);
            const Math::float4x4 worldMatrix =
                Math::y_axis_matrix(Math::k_pi) *
                Math::xyz_rotate_matrix(a_transform.rotation) *
                Math::translate_matrix(a_transform.position);

            Cue::ShadowSystem::SpotShadowItem item{};
            item.shadow.view = Math::float4x4::inverse(worldMatrix);
            item.shadow.projection =
                Math::perspective_fov_matrix(fovY, 1.0f, 0.05f, range);
            item.shadow.params = Math::float4(
                1.0f,
                (std::max)(spotLight->shadowBias, 0.0f) / range,
                static_cast<float>(GpuData::k_spotShadowMapSize),
                static_cast<float>(currentSpotLightIndex));
            m_currentCollector->submit_spot_shadow(item);
            m_hasSpotShadow = true;
        }

    private:
        Cue::ShadowSystem::ShadowScene& m_shadowScene;
        Cue::ShadowSystem::ShadowCollector* m_currentCollector = nullptr;
        uint32_t m_spotLightIndex = 0;
        bool m_hasSpotShadow = false;
    };
}
