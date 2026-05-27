#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <EffectSystem/EffectAsset.h>
#include <GameCore/Components.h>
#include <GpuData/Particle.h>
#include <ParticleSystem/ParticleRangeAllocator.h>
#include <ParticleSystem/ParticleScene.h>

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

namespace Cue::ECS
{
    class EffectEmitterSystem final
        : public ECSManager::System<WorldTransformComponent, EffectEmitterComponent>
    {
    public:
        explicit EffectEmitterSystem(
            AssetManager* a_assetManager,
            MaterialHandle a_defaultMaterialHandle,
            ParticleSystem::ParticleRangeAllocator& a_particleAllocator,
            ParticleSystem::ParticleScene& a_scene)
            : ECSManager::System<WorldTransformComponent, EffectEmitterComponent>(
                  [this](Entity a_entity,
                      WorldTransformComponent& a_transform,
                      EffectEmitterComponent& a_effect,
                      const UpdateContext& a_context)
                  {
                      collect_effect(a_entity, a_transform, a_effect, a_context);
                  })
            , m_assetManager(a_assetManager)
            , m_defaultMaterialHandle(a_defaultMaterialHandle)
            , m_particleAllocator(a_particleAllocator)
            , m_scene(a_scene)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentBufferIndex = a_context.bufferIndex;
            ECSManager::System<WorldTransformComponent, EffectEmitterComponent>::
                update(a_context);
            m_currentBufferIndex = 0;
        }

    private:
        static constexpr uint32_t k_invalidParticleBase =
            (std::numeric_limits<uint32_t>::max)();
        static constexpr uint32_t k_maxSpawnPerFrame = 64;

        [[nodiscard]] uint32_t allocate_particle_range(
            EffectEmitterRuntimeState& a_runtime,
            uint32_t a_requestedCapacity)
        {
            if (a_runtime.particleBase != k_invalidParticleBase &&
                a_runtime.particleCapacity > 0)
            {
                if (a_runtime.particleCapacity < a_requestedCapacity)
                {
                    m_particleAllocator.release(
                        a_runtime.particleBase,
                        a_runtime.particleCapacity);
                    a_runtime = {};
                    a_runtime.particleBase = k_invalidParticleBase;
                }
                else
                {
                    return a_runtime.particleCapacity;
                }
            }

            if (a_runtime.particleBase != k_invalidParticleBase &&
                a_runtime.particleCapacity > 0)
            {
                return a_runtime.particleCapacity;
            }

            uint32_t particleBase = k_invalidParticleBase;
            uint32_t capacity = 0;
            if (!m_particleAllocator.allocate(
                    a_requestedCapacity,
                    particleBase,
                    capacity))
            {
                return 0;
            }

            a_runtime.particleBase = particleBase;
            a_runtime.particleCapacity = capacity;
            return capacity;
        }

        void release_particle_range(EffectEmitterRuntimeState& a_runtime)
        {
            if (a_runtime.particleBase == k_invalidParticleBase ||
                a_runtime.particleCapacity == 0)
            {
                return;
            }

            m_particleAllocator.release(
                a_runtime.particleBase,
                a_runtime.particleCapacity);
            a_runtime = {};
            a_runtime.particleBase = k_invalidParticleBase;
        }

        void release_effect_ranges(EffectEmitterComponent& a_effect)
        {
            for (EffectEmitterRuntimeState& runtime : a_effect.runtimeEmitters)
            {
                release_particle_range(runtime);
            }
        }

        [[nodiscard]] uint32_t calculate_spawn_count(
            const EffectSystem::EffectEmitterDesc& a_emitter,
            EffectEmitterRuntimeState& a_runtime,
            bool a_isPlaying,
            float a_deltaTime)
        {
            if (!a_isPlaying || a_deltaTime <= 0.0f)
            {
                return 0;
            }

            a_runtime.timeSeconds += a_deltaTime;
            float localTime = a_runtime.timeSeconds - a_emitter.startDelay;
            if (localTime < 0.0f)
            {
                return 0;
            }
            if (a_emitter.duration > 0.0f && localTime > a_emitter.duration)
            {
                if (!a_emitter.isLooping)
                {
                    return 0;
                }

                localTime = std::fmod(localTime, a_emitter.duration);
                a_runtime.timeSeconds = a_emitter.startDelay + localTime;
                a_runtime.hasSpawnedBurst = false;
            }

            uint32_t spawnCount = 0;
            if (!a_runtime.hasSpawnedBurst)
            {
                spawnCount += a_emitter.burstCount;
                a_runtime.hasSpawnedBurst = true;
            }

            if (a_emitter.emitRate > 0.0f)
            {
                a_runtime.emitAccumulator += a_emitter.emitRate * a_deltaTime;
                const uint32_t continuousCount =
                    static_cast<uint32_t>(a_runtime.emitAccumulator);
                if (continuousCount > 0)
                {
                    spawnCount += continuousCount;
                    a_runtime.emitAccumulator -=
                        static_cast<float>(continuousCount);
                }
            }

            return (std::min)(spawnCount, k_maxSpawnPerFrame);
        }

        [[nodiscard]] static Math::float4 make_shape_params(
            const EffectSystem::EffectEmitterDesc& a_emitter) noexcept
        {
            if (a_emitter.shape == EffectSystem::EffectEmitterShape::Sphere)
            {
                return Math::float4(a_emitter.shapeRadius, 0.0f, 0.0f, 0.0f);
            }
            if (a_emitter.shape == EffectSystem::EffectEmitterShape::Box)
            {
                return Math::float4(
                    a_emitter.shapeBoxExtents.x,
                    a_emitter.shapeBoxExtents.y,
                    a_emitter.shapeBoxExtents.z,
                    0.0f);
            }
            if (a_emitter.shape == EffectSystem::EffectEmitterShape::Cone)
            {
                return Math::float4(
                    a_emitter.shapeRadius,
                    a_emitter.shapeAngleDegrees,
                    0.0f,
                    0.0f);
            }

            return Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        [[nodiscard]] MaterialHandle resolve_material_handle(
            const EffectEmitterComponent& a_effect,
            const EffectSystem::EffectEmitterDesc& a_emitter) const
        {
            if (a_effect.overrideMaterialHandle.valid())
            {
                return a_effect.overrideMaterialHandle;
            }

            MaterialHandle materialHandle{};
            if (!a_emitter.materialName.empty() && m_assetManager != nullptr &&
                m_assetManager->get_material(a_emitter.materialName, materialHandle))
            {
                return materialHandle;
            }

            return m_defaultMaterialHandle;
        }

        void collect_emitter(
            const WorldTransformComponent& a_transform,
            EffectEmitterComponent& a_effect,
            const EffectSystem::EffectEmitterDesc& a_emitter,
            EffectEmitterRuntimeState& a_runtime,
            uint32_t a_emitterIndex,
            const UpdateContext& a_context)
        {
            if (!a_emitter.isVisible)
            {
                return;
            }

            const uint32_t capacity =
                allocate_particle_range(a_runtime, a_emitter.maxParticleCount);
            if (capacity == 0)
            {
                return;
            }

            Math::float4 materialColor(1.0f, 1.0f, 1.0f, 1.0f);
            uint32_t textureId = 0;
            uint32_t useTexture = 0;
            const MaterialHandle materialHandle =
                resolve_material_handle(a_effect, a_emitter);
            if (materialHandle.valid() && m_assetManager != nullptr)
            {
                MaterialDesc materialDesc{};
                if (m_assetManager->get_material(materialHandle, materialDesc))
                {
                    materialColor = materialDesc.color;
                    textureId = materialDesc.textureId;
                    useTexture = materialDesc.isTextureUsed ? 1u : 0u;
                }
            }

            const uint32_t spawnCount = calculate_spawn_count(
                a_emitter,
                a_runtime,
                a_effect.isPlaying,
                a_context.deltaTime);

            ParticleSystem::ParticleEmitterItem item{};
            item.emitter.position = Math::float3(
                a_transform.position.x + a_emitter.positionOffset.x,
                a_transform.position.y + a_emitter.positionOffset.y,
                a_transform.position.z + a_emitter.positionOffset.z);
            item.emitter.particleBase = a_runtime.particleBase;
            item.emitter.velocityMin = a_emitter.velocityMin;
            item.emitter.particleCapacity = capacity;
            item.emitter.velocityMax = a_emitter.velocityMax;
            item.emitter.spawnCount = spawnCount;
            item.emitter.acceleration = a_emitter.acceleration;
            item.emitter.spawnCursor = a_runtime.spawnCursor;
            item.emitter.startColor = Math::float4(
                a_emitter.startColor.r * materialColor.r,
                a_emitter.startColor.g * materialColor.g,
                a_emitter.startColor.b * materialColor.b,
                a_emitter.startColor.a * materialColor.a);
            item.emitter.midColor = Math::float4(
                a_emitter.midColor.r * materialColor.r,
                a_emitter.midColor.g * materialColor.g,
                a_emitter.midColor.b * materialColor.b,
                a_emitter.midColor.a * materialColor.a);
            item.emitter.endColor = Math::float4(
                a_emitter.endColor.r * materialColor.r,
                a_emitter.endColor.g * materialColor.g,
                a_emitter.endColor.b * materialColor.b,
                a_emitter.endColor.a * materialColor.a);
            item.emitter.sizeLifetime = Math::float4(
                a_emitter.startSize,
                a_emitter.endSize,
                a_emitter.minLifetime,
                a_emitter.maxLifetime);
            item.emitter.curveParams = Math::float4(
                a_emitter.midSize,
                a_emitter.curveMidTime,
                0.0f,
                0.0f);
            item.emitter.shapeParams = make_shape_params(a_emitter);
            item.emitter.trailParams = Math::float4(
                a_emitter.trailWidth,
                a_emitter.trailLength,
                static_cast<float>(a_emitter.trailSegmentCount),
                a_emitter.meshScale);
            item.emitter.forceParams = Math::float4(
                a_emitter.drag,
                a_emitter.noiseStrength,
                a_emitter.noiseFrequency,
                a_emitter.vortexStrength);
            item.emitter.attractorParams = Math::float4(
                a_emitter.attractorPosition.x,
                a_emitter.attractorPosition.y,
                a_emitter.attractorPosition.z,
                a_emitter.attractorStrength);
            item.emitter.linearForce = Math::float4(
                a_emitter.linearForce.x,
                a_emitter.linearForce.y,
                a_emitter.linearForce.z,
                0.0f);
            item.emitter.textureId = textureId;
            item.emitter.useTexture = useTexture;
            item.emitter.rendererType =
                static_cast<uint32_t>(a_emitter.rendererType);
            item.emitter.shapeType = static_cast<uint32_t>(a_emitter.shape);
            item.emitter.randomSeed =
                a_emitter.randomSeed ^ a_effect.randomSeed ^ a_emitterIndex;
            item.emitter.billboardMode =
                static_cast<uint32_t>(a_emitter.billboardMode);

            if (m_scene.submit_emitter(m_currentBufferIndex, item) &&
                spawnCount > 0)
            {
                a_runtime.spawnCursor =
                    (a_runtime.spawnCursor + spawnCount) % capacity;
            }
        }

        void collect_effect(Entity a_entity,
            WorldTransformComponent& a_transform,
            EffectEmitterComponent& a_effect,
            const UpdateContext& a_context)
        {
            a_entity;
            if (!a_effect.isVisible || m_assetManager == nullptr)
            {
                release_effect_ranges(a_effect);
                return;
            }

            EffectHandle effectHandle = a_effect.effectHandle;
            if (!effectHandle.valid() && !a_effect.effectName.empty())
            {
                (void)m_assetManager->get_effect(
                    a_effect.effectName,
                    effectHandle);
                a_effect.effectHandle = effectHandle;
            }
            if (!effectHandle.valid())
            {
                release_effect_ranges(a_effect);
                return;
            }

            const EffectSystem::EffectAsset* asset = nullptr;
            if (!m_assetManager->get_effect(effectHandle, asset) ||
                asset == nullptr)
            {
                release_effect_ranges(a_effect);
                return;
            }

            if (a_effect.runtimeEmitters.size() > asset->emitters.size())
            {
                for (size_t emitterIndex = asset->emitters.size();
                     emitterIndex < a_effect.runtimeEmitters.size();
                     ++emitterIndex)
                {
                    release_particle_range(
                        a_effect.runtimeEmitters[emitterIndex]);
                }
                a_effect.runtimeEmitters.resize(asset->emitters.size());
            }
            if (a_effect.runtimeEmitters.size() < asset->emitters.size())
            {
                a_effect.runtimeEmitters.resize(asset->emitters.size());
            }

            const float playbackSpeed =
                (std::max)(a_effect.playbackSpeed, 0.0f);
            UpdateContext effectContext = a_context;
            effectContext.deltaTime *= playbackSpeed;

            for (uint32_t emitterIndex = 0;
                 emitterIndex < static_cast<uint32_t>(asset->emitters.size());
                 ++emitterIndex)
            {
                collect_emitter(
                    a_transform,
                    a_effect,
                    asset->emitters[emitterIndex],
                    a_effect.runtimeEmitters[emitterIndex],
                    emitterIndex,
                    effectContext);
            }
        }

        AssetManager* m_assetManager = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        ParticleSystem::ParticleRangeAllocator& m_particleAllocator;
        ParticleSystem::ParticleScene& m_scene;
        uint32_t m_currentBufferIndex = 0;
    };
} // namespace Cue::ECS
