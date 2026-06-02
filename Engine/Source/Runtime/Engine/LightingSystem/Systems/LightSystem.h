// LightSystem の役割と公開要素を定義する

#pragma once

// === LightingSystem includes ===
#include <LightingSystem/LightCollector.h>
#include <LightingSystem/LightScene.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>

namespace Cue::ECS
{
    class LightSystem final
        : public ECSManager::System<WorldTransformComponent>
    {
    public:
        explicit LightSystem(LightingSystem::LightScene& a_lightScene)
            : ECSManager::System<WorldTransformComponent>(
                  [this](Entity a_entity,
                      WorldTransformComponent& a_transform,
                      const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_transform, a_context);
                  }),
            m_lightScene(a_lightScene)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            LightingSystem::LightCollector collector(
                m_lightScene, a_context.bufferIndex);
            m_currentCollector = &collector;
            ECSManager::System<WorldTransformComponent>::update(a_context);
            m_currentCollector = nullptr;
        }

    private:
        [[nodiscard]] static Math::float3 transform_direction(
            const Math::float3& a_direction,
            const Math::Quaternion& a_rotation) noexcept
        {
            const Math::float4x4 rotationMatrix =
                Math::quaternion_matrix(a_rotation);
            Math::float3 direction(
                a_direction.x * rotationMatrix.values[0][0] +
                    a_direction.y * rotationMatrix.values[1][0] +
                    a_direction.z * rotationMatrix.values[2][0],
                a_direction.x * rotationMatrix.values[0][1] +
                    a_direction.y * rotationMatrix.values[1][1] +
                    a_direction.z * rotationMatrix.values[2][1],
                a_direction.x * rotationMatrix.values[0][2] +
                    a_direction.y * rotationMatrix.values[1][2] +
                    a_direction.z * rotationMatrix.values[2][2]);
            direction.normalize();
            return direction;
        }

        [[nodiscard]] static Math::float3 light_direction(
            const WorldTransformComponent& a_transform) noexcept
        {
            return transform_direction(
                Math::float3(0.0f, 0.0f, -1.0f),
                a_transform.rotation);
        }

        void update_component(
            Entity a_entity,
            WorldTransformComponent& a_transform,
            const UpdateContext& a_context)
        {
            a_entity;
            a_context;

            if (m_currentCollector == nullptr || this->m_pEcs == nullptr)
            {
                return;
            }

            DirectionalLightComponent* directionalLight =
                this->m_pEcs->get_component<DirectionalLightComponent>(a_entity);
            if (directionalLight != nullptr &&
                directionalLight->isEnabled)
            {
                LightingSystem::DirectionalLightItem item{};
                const Math::float3 direction = light_direction(a_transform);
                item.light.directionIntensity = Math::float4(
                    direction.x,
                    direction.y,
                    direction.z,
                    (std::max)(directionalLight->intensity, 0.0f));
                item.light.color = Math::float4(
                    directionalLight->color.x,
                    directionalLight->color.y,
                    directionalLight->color.z,
                    1.0f);
                m_currentCollector->submit_directional(item);
            }

            PointLightComponent* pointLight =
                this->m_pEcs->get_component<PointLightComponent>(a_entity);
            if (pointLight != nullptr &&
                pointLight->isEnabled)
            {
                LightingSystem::PointLightItem item{};
                item.light.positionRange = Math::float4(
                    a_transform.position.x,
                    a_transform.position.y,
                    a_transform.position.z,
                    (std::max)(pointLight->range, 0.001f));
                item.light.colorIntensity = Math::float4(
                    pointLight->color.x,
                    pointLight->color.y,
                    pointLight->color.z,
                    (std::max)(pointLight->intensity, 0.0f));
                m_currentCollector->submit_point(item);
            }

            SpotLightComponent* spotLight =
                this->m_pEcs->get_component<SpotLightComponent>(a_entity);
            if (spotLight != nullptr &&
                spotLight->isEnabled)
            {
                LightingSystem::SpotLightItem item{};
                const Math::float3 direction = light_direction(a_transform);
                const float outerAngle = std::clamp(
                    spotLight->outerAngleDegrees,
                    1.0f,
                    89.0f);
                item.light.positionRange = Math::float4(
                    a_transform.position.x,
                    a_transform.position.y,
                    a_transform.position.z,
                    (std::max)(spotLight->range, 0.001f));
                item.light.directionOuterCos = Math::float4(
                    direction.x,
                    direction.y,
                    direction.z,
                    std::cos(outerAngle * Math::k_pi / 180.0f));
                item.light.colorIntensity = Math::float4(
                    spotLight->color.x,
                    spotLight->color.y,
                    spotLight->color.z,
                    (std::max)(spotLight->intensity, 0.0f));
                m_currentCollector->submit_spot(item);
            }
        }

    private:
        LightingSystem::LightScene& m_lightScene;
        LightingSystem::LightCollector* m_currentCollector = nullptr;
    };
}
