#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Base includes ===
#include <CueAssert.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GpuData/Lighting.h>

// === RHI includes ===
#include <RHI.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <vector>

namespace Cue::ECS
{
    class LightSystem final
        : public ECSManager::System<TransformComponent, LightComponent>
    {
    public:
        explicit LightSystem(
            std::vector<RHI::SlotUploader<GpuData::DirectionalLightGpu>>&
                a_lightUploaders,
            std::vector<RHI::SlotUploader<GpuData::ShadowMappingGpu>>&
                a_shadowUploaders) noexcept
            : ECSManager::System<TransformComponent, LightComponent>(
                  [this](Entity,
                      TransformComponent& a_transform,
                      LightComponent& a_light,
                      const UpdateContext&)
                  {
                      update_component(a_transform, a_light);
                  })
            , m_lightUploaders(a_lightUploaders)
            , m_shadowUploaders(a_shadowUploaders)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentLight = make_default_light();
            m_hasDirectionalLight = false;
            m_currentUploader = nullptr;
            m_currentShadowUploader = nullptr;
            if (a_context.bufferIndex < m_lightUploaders.size())
            {
                m_currentUploader = &m_lightUploaders[a_context.bufferIndex];
                m_currentUploader->begin_frame();
            }
            if (a_context.bufferIndex < m_shadowUploaders.size())
            {
                m_currentShadowUploader =
                    &m_shadowUploaders[a_context.bufferIndex];
                m_currentShadowUploader->begin_frame();
            }

            ECSManager::System<TransformComponent, LightComponent>::update(
                a_context);

            if (m_currentUploader == nullptr)
            {
                return;
            }

            if (!m_currentUploader->push(0, m_currentLight))
            {
                CUE_ASSERTF(false, "Failed to queue directional light upload.");
                return;
            }
            if (!m_currentUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit directional light upload.");
            }

            if (m_currentShadowUploader == nullptr)
            {
                return;
            }

            const GpuData::ShadowMappingGpu shadow = make_shadow_mapping(
                Math::float3(m_currentLight.directionAndIntensity.x,
                    m_currentLight.directionAndIntensity.y,
                    m_currentLight.directionAndIntensity.z));
            if (!m_currentShadowUploader->push(0, shadow))
            {
                CUE_ASSERTF(false, "Failed to queue shadow mapping upload.");
                return;
            }
            if (!m_currentShadowUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit shadow mapping upload.");
            }
        }

    private:
        [[nodiscard]] static GpuData::DirectionalLightGpu make_default_light()
            noexcept
        {
            GpuData::DirectionalLightGpu light{};
            Math::float3 direction(-0.4f, -0.7f, -0.6f);
            direction.normalize();
            light.directionAndIntensity =
                Math::float4(direction.x, direction.y, direction.z, 1.0f);
            light.colorAndAmbient =
                Math::float4(1.0f, 0.96f, 0.88f, 0.18f);
            light.ambientGroundAndSpecular =
                Math::float4(0.08f, 0.09f, 0.11f, 1.0f);
            return light;
        }

        [[nodiscard]] static Math::float4x4 look_to_view_matrix(
            Math::float3 a_eye,
            Math::float3 a_forward,
            Math::float3 a_up) noexcept
        {
            a_forward.normalize();
            Math::float3 right = Math::float3::cross(a_up, a_forward);
            right.normalize();
            Math::float3 up = Math::float3::cross(a_forward, right);
            up.normalize();

            Math::float4x4 view = Math::float4x4::identity();
            view.values[0][0] = right.x;
            view.values[1][0] = right.y;
            view.values[2][0] = right.z;
            view.values[0][1] = up.x;
            view.values[1][1] = up.y;
            view.values[2][1] = up.z;
            view.values[0][2] = a_forward.x;
            view.values[1][2] = a_forward.y;
            view.values[2][2] = a_forward.z;
            view.values[3][0] = -Math::float3::dot(a_eye, right);
            view.values[3][1] = -Math::float3::dot(a_eye, up);
            view.values[3][2] = -Math::float3::dot(a_eye, a_forward);
            return view;
        }

        [[nodiscard]] static GpuData::ShadowMappingGpu make_shadow_mapping(
            Math::float3 a_lightDirection) noexcept
        {
            if (a_lightDirection.length_sq() <= 0.0001f)
            {
                a_lightDirection = Math::float3(-0.4f, -0.7f, -0.6f);
            }
            a_lightDirection.normalize();

            Math::float3 up(0.0f, 1.0f, 0.0f);
            if (std::fabs(Math::float3::dot(up, a_lightDirection)) > 0.95f)
            {
                up = Math::float3(0.0f, 0.0f, 1.0f);
            }

            constexpr float k_shadowMapSize = 1024.0f;
            constexpr float k_halfExtent = 24.0f;
            constexpr float k_lightDistance = 32.0f;
            constexpr float k_nearClip = 0.1f;
            constexpr float k_farClip = 80.0f;
            constexpr float k_depthBias = 0.006f;
            constexpr float k_shadowStrength = 0.38f;
            const Math::float3 center = Math::float3::zero();
            const Math::float3 eye =
                center - (a_lightDirection * k_lightDistance);

            GpuData::ShadowMappingGpu shadow{};
            shadow.view = look_to_view_matrix(eye, a_lightDirection, up);
            shadow.projection = Math::orthographic_matrix(
                -k_halfExtent,
                k_halfExtent,
                k_halfExtent,
                -k_halfExtent,
                k_nearClip,
                k_farClip);
            shadow.texelSizeAndBias = Math::float4(
                1.0f / k_shadowMapSize,
                1.0f / k_shadowMapSize,
                k_depthBias,
                k_shadowStrength);
            return shadow;
        }

        [[nodiscard]] static Math::float3 forward_from_rotation(
            const Math::float3& a_rotation) noexcept
        {
            const float sinPitch = std::sin(a_rotation.x);
            const float cosPitch = std::cos(a_rotation.x);
            const float sinYaw = std::sin(a_rotation.y);
            const float cosYaw = std::cos(a_rotation.y);
            Math::float3 direction(
                cosPitch * sinYaw,
                -sinPitch,
                cosPitch * cosYaw);
            direction.normalize();
            return direction;
        }

        void update_component(
            const TransformComponent& a_transform,
            const LightComponent& a_light) noexcept
        {
            if (m_hasDirectionalLight || !a_light.isEnabled ||
                a_light.type != LightType::Directional)
            {
                return;
            }

            const Math::float3 direction =
                forward_from_rotation(a_transform.rotation);
            const float intensity = (std::max)(a_light.intensity, 0.0f);
            const float ambient =
                (std::max)(a_light.ambient, 0.0f);

            m_currentLight.directionAndIntensity =
                Math::float4(direction.x, direction.y, direction.z, intensity);
            m_currentLight.colorAndAmbient = Math::float4(
                a_light.color.x, a_light.color.y, a_light.color.z, ambient);
            m_currentLight.ambientGroundAndSpecular =
                Math::float4(a_light.groundAmbient.x,
                    a_light.groundAmbient.y,
                    a_light.groundAmbient.z,
                    1.0f);
            m_hasDirectionalLight = true;
        }

        std::vector<RHI::SlotUploader<GpuData::DirectionalLightGpu>>&
            m_lightUploaders;
        std::vector<RHI::SlotUploader<GpuData::ShadowMappingGpu>>&
            m_shadowUploaders;
        RHI::SlotUploader<GpuData::DirectionalLightGpu>* m_currentUploader =
            nullptr;
        RHI::SlotUploader<GpuData::ShadowMappingGpu>* m_currentShadowUploader =
            nullptr;
        GpuData::DirectionalLightGpu m_currentLight{};
        bool m_hasDirectionalLight = false;
    };
}
