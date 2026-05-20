#pragma once

// === ShadowSystem includes ===
#include <ShadowSystem/ShadowAtlasAllocator.h>
#include <ShadowSystem/ShadowCollector.h>
#include <ShadowSystem/ShadowScene.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

namespace Cue::ECS
{
    class ShadowSystem final
        : public ECSManager::System<WorldTransformComponent>
    {
    public:
        explicit ShadowSystem(Cue::ShadowSystem::ShadowScene& a_shadowScene)
            : ECSManager::System<WorldTransformComponent>(
                  [this](Entity a_entity,
                      WorldTransformComponent& a_transform,
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
            m_directionalLightIndex = 0;
            m_pointLightIndex = 0;
            m_hasDirectionalShadow = false;
            m_hasPointShadow = false;
            m_spotLightIndex = 0;
            m_spotShadowCandidates.clear();
            ECSManager::System<WorldTransformComponent>::update(a_context);
            submit_spot_shadow_candidates();
            m_currentCollector = nullptr;
        }

    private:
        struct SpotShadowCandidate final
        {
            Cue::ShadowSystem::SpotShadowItem item{};
            float priority = 0.0f;
            uint32_t lightIndex = 0;
        };

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
                this->m_pEcs->get_component<DirectionalLightComponent>(
                    a_entity);
            if (directionalLight != nullptr && directionalLight->isEnabled)
            {
                const uint32_t currentDirectionalLightIndex =
                    m_directionalLightIndex;
                ++m_directionalLightIndex;

                if (directionalLight->castsShadow && !m_hasDirectionalShadow)
                {
                    const float halfSize =
                        (std::max)(directionalLight->shadowSize, 0.001f) *
                        0.5f;
                    const float distance =
                        (std::max)(directionalLight->shadowDistance, 0.001f);
                    const float depthRange = distance * 2.0f;
                    const Math::float4x4 worldMatrix =
                        Math::y_axis_matrix(Math::k_pi) *
                        Math::quaternion_matrix(a_transform.rotation) *
                        Math::translate_matrix(a_transform.position);

                    Cue::ShadowSystem::DirectionalShadowItem item{};
                    item.shadow.view = Math::float4x4::inverse(worldMatrix);
                    item.shadow.projection = Math::orthographic_matrix(
                        -halfSize,
                        halfSize,
                        halfSize,
                        -halfSize,
                        -distance,
                        distance);
                    item.shadow.params = Math::float4(
                        1.0f,
                        (std::max)(directionalLight->shadowBias, 0.0f) /
                            depthRange,
                        0.0f,
                        static_cast<float>(currentDirectionalLightIndex));
                    item.shadow.tuning = Math::float4(
                        static_cast<float>(GpuData::k_directionalShadowMapSize),
                        std::clamp(
                            directionalLight->shadowStrength, 0.0f, 1.0f),
                        (std::max)(directionalLight->shadowSoftness, 0.0f),
                        (std::max)(directionalLight->shadowSlopeBias, 0.0f) /
                            depthRange);
                    m_currentCollector->submit_directional_shadow(item);
                    m_hasDirectionalShadow = true;
                }
            }

            PointLightComponent* pointLight =
                this->m_pEcs->get_component<PointLightComponent>(a_entity);
            if (pointLight != nullptr && pointLight->isEnabled)
            {
                const uint32_t currentPointLightIndex = m_pointLightIndex;
                ++m_pointLightIndex;

                if (pointLight->castsShadow && !m_hasPointShadow)
                {
                    const float range = (std::max)(pointLight->range, 0.001f);
                    const float nearClip = std::clamp(
                        pointLight->shadowNearClip,
                        0.001f,
                        (std::max)(range - 0.001f, 0.001f));
                    Cue::ShadowSystem::PointShadowItem item{};
                    for (uint32_t faceIndex = 0;
                         faceIndex < GpuData::k_pointShadowFaceCount;
                         ++faceIndex)
                    {
                        const uint32_t tileX =
                            faceIndex % GpuData::k_pointShadowAtlasColumnCount;
                        const uint32_t tileY =
                            faceIndex / GpuData::k_pointShadowAtlasColumnCount;
                        GpuData::PointShadowFaceGpu& face =
                            item.faces[faceIndex];
                        face.view = Math::float4x4::inverse(
                            point_shadow_face_matrix(
                                a_transform.position,
                                faceIndex));
                        face.projection = Math::perspective_fov_matrix(
                            Math::k_pi * 0.5f,
                            1.0f,
                            nearClip,
                            range);
                        face.atlas = Math::float4(
                            static_cast<float>(tileX) /
                                static_cast<float>(
                                    GpuData::k_pointShadowAtlasColumnCount),
                            static_cast<float>(tileY) /
                                static_cast<float>(
                                    GpuData::k_pointShadowAtlasRowCount),
                            1.0f /
                                static_cast<float>(
                                    GpuData::k_pointShadowAtlasColumnCount),
                            1.0f /
                                static_cast<float>(
                                    GpuData::k_pointShadowAtlasRowCount));
                        face.params = Math::float4(
                            1.0f,
                            (std::max)(pointLight->shadowBias, 0.0f) / range,
                            nearClip,
                            static_cast<float>(currentPointLightIndex));
                        face.tuning = Math::float4(
                            static_cast<float>(GpuData::k_pointShadowTileSize),
                            std::clamp(pointLight->shadowStrength, 0.0f, 1.0f),
                            (std::max)(pointLight->shadowSoftness, 0.0f),
                            (std::max)(pointLight->shadowSlopeBias, 0.0f) /
                                range);
                        face.lightPositionRange = Math::float4(
                            a_transform.position.x,
                            a_transform.position.y,
                            a_transform.position.z,
                            range);
                    }

                    m_currentCollector->submit_point_shadow(item);
                    m_hasPointShadow = true;
                }
            }

            SpotLightComponent* spotLight =
                this->m_pEcs->get_component<SpotLightComponent>(a_entity);
            if (spotLight == nullptr || !spotLight->isEnabled)
            {
                return;
            }

            const uint32_t currentSpotLightIndex = m_spotLightIndex;
            ++m_spotLightIndex;

            if (!spotLight->castsShadow)
            {
                return;
            }

            const float outerAngle = std::clamp(
                spotLight->outerAngleDegrees,
                1.0f,
                89.0f);
            const float fovY = Math::degrees_to_radians(outerAngle * 2.0f);
            const float range = (std::max)(spotLight->range, 0.001f);
            const float nearClip = std::clamp(
                spotLight->shadowNearClip,
                0.001f,
                (std::max)(range - 0.001f, 0.001f));
            const Math::float4x4 worldMatrix =
                Math::y_axis_matrix(Math::k_pi) *
                Math::quaternion_matrix(a_transform.rotation) *
                Math::translate_matrix(a_transform.position);

            SpotShadowCandidate candidate{};
            candidate.item.shadow.view = Math::float4x4::inverse(worldMatrix);
            candidate.item.shadow.projection =
                Math::perspective_fov_matrix(fovY, 1.0f, nearClip, range);
            candidate.item.shadow.params = Math::float4(
                1.0f,
                (std::max)(spotLight->shadowBias, 0.0f) / range,
                nearClip,
                static_cast<float>(currentSpotLightIndex));
            candidate.item.shadow.tuning = Math::float4(
                static_cast<float>(GpuData::k_spotShadowTileSize),
                std::clamp(spotLight->shadowStrength, 0.0f, 1.0f),
                (std::max)(spotLight->shadowSoftness, 0.0f),
                (std::max)(spotLight->shadowSlopeBias, 0.0f) / range);
            candidate.priority =
                (std::max)(spotLight->intensity, 0.0f) * range;
            candidate.lightIndex = currentSpotLightIndex;
            m_spotShadowCandidates.push_back(candidate);
        }

        void submit_spot_shadow_candidates()
        {
            if (m_currentCollector == nullptr)
            {
                return;
            }

            std::sort(
                m_spotShadowCandidates.begin(),
                m_spotShadowCandidates.end(),
                [](const SpotShadowCandidate& a_left,
                    const SpotShadowCandidate& a_right)
                {
                    if (a_left.priority == a_right.priority)
                    {
                        return a_left.lightIndex < a_right.lightIndex;
                    }

                    return a_left.priority > a_right.priority;
                });

            m_atlasAllocator.reset();
            for (SpotShadowCandidate& candidate : m_spotShadowCandidates)
            {
                uint32_t slot = 0;
                Math::float4 atlas{};
                if (!m_atlasAllocator.allocate(slot, atlas))
                {
                    break;
                }

                candidate.item.shadow.atlas = atlas;
                m_currentCollector->submit_spot_shadow(candidate.item);
            }
        }

        [[nodiscard]] static Math::float4x4 point_shadow_face_matrix(
            const Math::float3& a_position,
            uint32_t a_faceIndex) noexcept
        {
            Math::float3 right(1.0f, 0.0f, 0.0f);
            Math::float3 up(0.0f, 1.0f, 0.0f);
            Math::float3 forward(0.0f, 0.0f, 1.0f);
            switch (a_faceIndex)
            {
            case 0:
                right = Math::float3(0.0f, 0.0f, -1.0f);
                up = Math::float3(0.0f, 1.0f, 0.0f);
                forward = Math::float3(1.0f, 0.0f, 0.0f);
                break;
            case 1:
                right = Math::float3(0.0f, 0.0f, 1.0f);
                up = Math::float3(0.0f, 1.0f, 0.0f);
                forward = Math::float3(-1.0f, 0.0f, 0.0f);
                break;
            case 2:
                right = Math::float3(1.0f, 0.0f, 0.0f);
                up = Math::float3(0.0f, 0.0f, -1.0f);
                forward = Math::float3(0.0f, 1.0f, 0.0f);
                break;
            case 3:
                right = Math::float3(1.0f, 0.0f, 0.0f);
                up = Math::float3(0.0f, 0.0f, 1.0f);
                forward = Math::float3(0.0f, -1.0f, 0.0f);
                break;
            case 4:
                right = Math::float3(1.0f, 0.0f, 0.0f);
                up = Math::float3(0.0f, 1.0f, 0.0f);
                forward = Math::float3(0.0f, 0.0f, 1.0f);
                break;
            default:
                right = Math::float3(-1.0f, 0.0f, 0.0f);
                up = Math::float3(0.0f, 1.0f, 0.0f);
                forward = Math::float3(0.0f, 0.0f, -1.0f);
                break;
            }

            Math::float4x4 matrix = Math::float4x4::identity();
            matrix.values[0][0] = right.x;
            matrix.values[0][1] = right.y;
            matrix.values[0][2] = right.z;
            matrix.values[1][0] = up.x;
            matrix.values[1][1] = up.y;
            matrix.values[1][2] = up.z;
            matrix.values[2][0] = forward.x;
            matrix.values[2][1] = forward.y;
            matrix.values[2][2] = forward.z;
            matrix.values[3][0] = a_position.x;
            matrix.values[3][1] = a_position.y;
            matrix.values[3][2] = a_position.z;
            return matrix;
        }

        Cue::ShadowSystem::ShadowScene& m_shadowScene;
        Cue::ShadowSystem::ShadowCollector* m_currentCollector = nullptr;
        Cue::ShadowSystem::ShadowAtlasAllocator m_atlasAllocator{};
        std::vector<SpotShadowCandidate> m_spotShadowCandidates{};
        uint32_t m_directionalLightIndex = 0;
        uint32_t m_pointLightIndex = 0;
        uint32_t m_spotLightIndex = 0;
        bool m_hasDirectionalShadow = false;
        bool m_hasPointShadow = false;
    };
}
