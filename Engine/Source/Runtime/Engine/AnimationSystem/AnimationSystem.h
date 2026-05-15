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

                Math::float3 translation = Math::float3::zero();
                Math::Quaternion rotation = Math::Quaternion::identity();
                Math::float3 scale(1.0f, 1.0f, 1.0f);

                const Math::float4x4 bind =
                    a_modelData.skeletonJoints[channel.jointIndex]
                        .localBindMatrix;
                translation = Math::float3(
                    bind.values[3][0], bind.values[3][1], bind.values[3][2]);

                translation = sample_keys(
                    channel.translations, a_time, translation);
                rotation = sample_keys(channel.rotations, a_time, rotation);
                scale = sample_keys(channel.scales, a_time, scale);

                a_animation.localPose[channel.jointIndex] =
                    Math::make_affine_matrix(scale, rotation, translation);
            }

            std::vector<uint8_t> resolved(jointCount, 0u);
            std::vector<Math::float4x4> bindModelPose(
                jointCount,
                Math::float4x4::identity());
            std::vector<uint8_t> bindResolved(jointCount, 0u);
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
            auto resolve_bind_pose =
                [&](auto&& self, size_t a_jointIndex) -> Math::float4x4
            {
                if (bindResolved[a_jointIndex] != 0u)
                {
                    return bindModelPose[a_jointIndex];
                }

                const int32_t parentIndex =
                    a_modelData.skeletonJoints[a_jointIndex].parentIndex;
                if (parentIndex >= 0 &&
                    static_cast<size_t>(parentIndex) < jointCount)
                {
                    bindModelPose[a_jointIndex] =
                        a_modelData.skeletonJoints[a_jointIndex]
                            .localBindMatrix *
                        self(self, static_cast<size_t>(parentIndex));
                }
                else
                {
                    bindModelPose[a_jointIndex] =
                        a_modelData.skeletonJoints[a_jointIndex]
                            .localBindMatrix;
                }
                bindResolved[a_jointIndex] = 1u;
                return bindModelPose[a_jointIndex];
            };

            for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            {
                resolve_model_pose(resolve_model_pose, jointIndex);
                const Math::float4x4 inverseBind =
                    Math::float4x4::inverse(
                        resolve_bind_pose(resolve_bind_pose, jointIndex));
                a_animation.skinPalette[jointIndex] =
                    inverseBind * a_animation.modelPose[jointIndex];
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
