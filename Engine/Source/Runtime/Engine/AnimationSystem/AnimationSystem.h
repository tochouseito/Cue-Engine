#pragma once

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <type_traits>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>

namespace Cue::ECS
{
    class AnimationSystem final
        : public ECSManager::System<MeshFilterComponent, AnimationComponent>
    {
    public:
        explicit AnimationSystem(AssetManager* a_assetManager)
            : ECSManager::System<MeshFilterComponent, AnimationComponent>(
                  [this](Entity a_entity,
                      MeshFilterComponent& a_meshFilter,
                      AnimationComponent& a_animation,
                      const UpdateContext& a_context)
                  {
                      update_component(
                          a_entity, a_meshFilter, a_animation, a_context);
                  })
            , m_assetManager(a_assetManager)
        {
        }

    private:
        template <typename Keyframe, typename Value>
        [[nodiscard]] static Value sample_keys(
            const std::vector<Keyframe>& a_keys,
            float a_time,
            const Value& a_defaultValue)
        {
            if (a_keys.empty())
            {
                return a_defaultValue;
            }
            if (a_keys.size() == 1 || a_time <= a_keys.front().time)
            {
                return a_keys.front().value;
            }
            if (a_time >= a_keys.back().time)
            {
                return a_keys.back().value;
            }

            const auto nextIt = std::upper_bound(
                a_keys.begin(),
                a_keys.end(),
                a_time,
                [](float a_left, const Keyframe& a_right)
                {
                    return a_left < a_right.time;
                });
            const auto prevIt = nextIt - 1;
            const float duration = nextIt->time - prevIt->time;
            const float factor =
                duration > 0.0f ? (a_time - prevIt->time) / duration : 0.0f;
            if constexpr (std::is_same_v<Value, Math::Quaternion>)
            {
                return Math::Quaternion::slerp(
                    prevIt->value, nextIt->value, factor);
            }
            else
            {
                return Value::lerp(prevIt->value, nextIt->value, factor);
            }
        }

        [[nodiscard]] static float advance_time(
            const Core::Native::AnimationClipData& a_clip,
            AnimationComponent& a_animation,
            float a_deltaTime) noexcept
        {
            const float duration = (std::max)(a_clip.duration, 0.0f);
            if (!a_animation.isPlaying || duration <= 0.0f)
            {
                return (std::clamp)(a_animation.time, 0.0f, duration);
            }

            const float ticksPerSecond =
                a_clip.ticksPerSecond > 0.0f ? a_clip.ticksPerSecond : 1.0f;
            float nextTime =
                a_animation.time + a_deltaTime * a_animation.speed *
                                       ticksPerSecond;
            if (a_animation.loops)
            {
                nextTime = std::fmod(nextTime, duration);
                if (nextTime < 0.0f)
                {
                    nextTime += duration;
                }
            }
            else
            {
                nextTime = (std::clamp)(nextTime, 0.0f, duration);
            }

            a_animation.time = nextTime;
            ++a_animation.frame;
            return nextTime;
        }

        static void ensure_pose_storage(
            AnimationComponent& a_animation,
            size_t a_jointCount)
        {
            if (a_animation.localPose.size() != a_jointCount)
            {
                a_animation.localPose.assign(
                    a_jointCount, Math::float4x4::identity());
            }
            if (a_animation.modelPose.size() != a_jointCount)
            {
                a_animation.modelPose.assign(
                    a_jointCount, Math::float4x4::identity());
            }
            if (a_animation.skinPalette.size() != a_jointCount)
            {
                a_animation.skinPalette.assign(
                    a_jointCount, Math::float4x4::identity());
            }
        }

        struct TransformSample final
        {
            Math::float3 translation = Math::float3::zero();
            Math::Quaternion rotation = Math::Quaternion::identity();
            Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
        };

        [[nodiscard]] static float row_length(
            const Math::float4x4& a_matrix,
            uint32_t a_row) noexcept
        {
            return std::sqrt(
                a_matrix.values[a_row][0] * a_matrix.values[a_row][0] +
                a_matrix.values[a_row][1] * a_matrix.values[a_row][1] +
                a_matrix.values[a_row][2] * a_matrix.values[a_row][2]);
        }

        [[nodiscard]] static Math::Quaternion quaternion_from_matrix(
            const Math::float4x4& a_matrix) noexcept
        {
            const float trace =
                a_matrix.values[0][0] + a_matrix.values[1][1] +
                a_matrix.values[2][2];
            Math::Quaternion rotation{};
            if (trace > 0.0f)
            {
                const float s = std::sqrt(trace + 1.0f) * 2.0f;
                rotation.w = 0.25f * s;
                rotation.x = (a_matrix.values[1][2] -
                              a_matrix.values[2][1]) /
                             s;
                rotation.y = (a_matrix.values[2][0] -
                              a_matrix.values[0][2]) /
                             s;
                rotation.z = (a_matrix.values[0][1] -
                              a_matrix.values[1][0]) /
                             s;
            }
            else if (a_matrix.values[0][0] > a_matrix.values[1][1] &&
                     a_matrix.values[0][0] > a_matrix.values[2][2])
            {
                const float s =
                    std::sqrt(
                        1.0f + a_matrix.values[0][0] -
                        a_matrix.values[1][1] - a_matrix.values[2][2]) *
                    2.0f;
                rotation.w = (a_matrix.values[1][2] -
                              a_matrix.values[2][1]) /
                             s;
                rotation.x = 0.25f * s;
                rotation.y = (a_matrix.values[0][1] +
                              a_matrix.values[1][0]) /
                             s;
                rotation.z = (a_matrix.values[0][2] +
                              a_matrix.values[2][0]) /
                             s;
            }
            else if (a_matrix.values[1][1] > a_matrix.values[2][2])
            {
                const float s =
                    std::sqrt(
                        1.0f + a_matrix.values[1][1] -
                        a_matrix.values[0][0] - a_matrix.values[2][2]) *
                    2.0f;
                rotation.w = (a_matrix.values[2][0] -
                              a_matrix.values[0][2]) /
                             s;
                rotation.x = (a_matrix.values[0][1] +
                              a_matrix.values[1][0]) /
                             s;
                rotation.y = 0.25f * s;
                rotation.z = (a_matrix.values[1][2] +
                              a_matrix.values[2][1]) /
                             s;
            }
            else
            {
                const float s =
                    std::sqrt(
                        1.0f + a_matrix.values[2][2] -
                        a_matrix.values[0][0] - a_matrix.values[1][1]) *
                    2.0f;
                rotation.w = (a_matrix.values[0][1] -
                              a_matrix.values[1][0]) /
                             s;
                rotation.x = (a_matrix.values[0][2] +
                              a_matrix.values[2][0]) /
                             s;
                rotation.y = (a_matrix.values[1][2] +
                              a_matrix.values[2][1]) /
                             s;
                rotation.z = 0.25f * s;
            }

            return Math::Quaternion::normalize(rotation);
        }

        [[nodiscard]] static TransformSample decompose_transform(
            const Math::float4x4& a_matrix) noexcept
        {
            TransformSample transform{};
            transform.translation = Math::float3(
                a_matrix.values[3][0],
                a_matrix.values[3][1],
                a_matrix.values[3][2]);
            transform.scale = Math::float3(
                row_length(a_matrix, 0),
                row_length(a_matrix, 1),
                row_length(a_matrix, 2));

            Math::float4x4 rotationMatrix = a_matrix;
            for (uint32_t row = 0; row < 3; ++row)
            {
                const float scale =
                    row == 0 ? transform.scale.x
                    : row == 1 ? transform.scale.y
                               : transform.scale.z;
                if (scale > 0.0f)
                {
                    rotationMatrix.values[row][0] /= scale;
                    rotationMatrix.values[row][1] /= scale;
                    rotationMatrix.values[row][2] /= scale;
                }
            }
            rotationMatrix.values[3][0] = 0.0f;
            rotationMatrix.values[3][1] = 0.0f;
            rotationMatrix.values[3][2] = 0.0f;
            transform.rotation = quaternion_from_matrix(rotationMatrix);
            return transform;
        }

        static void evaluate_pose(
            const Core::Native::ModelData& a_modelData,
            const Core::Native::AnimationClipData& a_clip,
            float a_time,
            AnimationComponent& a_animation)
        {
            const size_t jointCount = a_modelData.skeletonJoints.size();
            ensure_pose_storage(a_animation, jointCount);

            for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            {
                a_animation.localPose[jointIndex] =
                    a_modelData.skeletonJoints[jointIndex].localBindMatrix;
            }

            for (const Core::Native::AnimationChannelData& channel :
                 a_clip.channels)
            {
                if (channel.jointIndex >= jointCount)
                {
                    continue;
                }

                const Math::float4x4 bind =
                    a_modelData.skeletonJoints[channel.jointIndex]
                        .localBindMatrix;
                const TransformSample bindTransform =
                    decompose_transform(bind);

                const Math::float3 translation = sample_keys(
                    channel.translations,
                    a_time,
                    bindTransform.translation);
                const Math::Quaternion rotation = sample_keys(
                    channel.rotations, a_time, bindTransform.rotation);
                const Math::float3 scale =
                    sample_keys(channel.scales, a_time, bindTransform.scale);

                a_animation.localPose[channel.jointIndex] =
                    Math::make_affine_matrix(scale, rotation, translation);
            }

            std::vector<uint8_t> resolved(jointCount, 0u);
            auto resolve_model_pose =
                [&](auto&& self, size_t a_jointIndex) -> Math::float4x4
            {
                if (resolved[a_jointIndex] != 0u)
                {
                    return a_animation.modelPose[a_jointIndex];
                }

                const int32_t parentIndex =
                    a_modelData.skeletonJoints[a_jointIndex].parentIndex;
                if (parentIndex >= 0 &&
                    static_cast<size_t>(parentIndex) < jointCount)
                {
                    a_animation.modelPose[a_jointIndex] =
                        a_animation.localPose[a_jointIndex] *
                        self(self, static_cast<size_t>(parentIndex));
                }
                else
                {
                    a_animation.modelPose[a_jointIndex] =
                        a_animation.localPose[a_jointIndex];
                }
                resolved[a_jointIndex] = 1u;
                return a_animation.modelPose[a_jointIndex];
            };

            for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            {
                resolve_model_pose(resolve_model_pose, jointIndex);
                a_animation.skinPalette[jointIndex] =
                    a_modelData.skeletonJoints[jointIndex].inverseBindMatrix *
                    a_animation.modelPose[jointIndex];
            }
        }

        void update_component(Entity a_entity,
            MeshFilterComponent& a_meshFilter,
            AnimationComponent& a_animation,
            const UpdateContext& a_context)
        {
            a_entity;
            if (m_assetManager == nullptr || a_meshFilter.modelName.empty())
            {
                return;
            }

            ModelHandle modelHandle{};
            Result result =
                m_assetManager->get_model(a_meshFilter.modelName, modelHandle);
            if (!result)
            {
                return;
            }

            Core::Native::ModelData modelData{};
            result = m_assetManager->get_model(modelHandle, modelData);
            if (!result || modelData.skeletonJoints.empty() ||
                modelData.animationClips.empty())
            {
                return;
            }

            uint32_t animationIndex = a_animation.animationIndex;
            if (animationIndex == Core::Native::k_invalidAnimationIndex ||
                animationIndex >= modelData.animationClips.size())
            {
                animationIndex = 0;
                a_animation.animationIndex = 0;
            }

            const Core::Native::AnimationClipData& clip =
                modelData.animationClips[animationIndex];
            const float time =
                advance_time(clip, a_animation, a_context.deltaTime);
            evaluate_pose(modelData, clip, time, a_animation);
        }

    private:
        AssetManager* m_assetManager = nullptr;
    };
}
