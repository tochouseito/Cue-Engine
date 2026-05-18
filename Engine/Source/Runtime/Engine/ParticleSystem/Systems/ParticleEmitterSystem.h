#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>
#include <GpuData/Particle.h>
#include <ParticleSystem/ParticleScene.h>

// === C++ includes ===
#include <algorithm>
#include <limits>

namespace Cue::ECS
{
    class ParticleEmitterSystem final
        : public ECSManager::System<TransformComponent, ParticleEmitterComponent>
    {
    public:
        explicit ParticleEmitterSystem(
            AssetManager* a_assetManager,
            MaterialHandle a_defaultMaterialHandle,
            ParticleSystem::ParticleScene& a_scene)
            : ECSManager::System<TransformComponent, ParticleEmitterComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      ParticleEmitterComponent& a_emitter,
                      const UpdateContext& a_context)
                  {
                      collect_emitter(
                          a_entity, a_transform, a_emitter, a_context);
                  })
            , m_assetManager(a_assetManager)
            , m_defaultMaterialHandle(a_defaultMaterialHandle)
            , m_scene(a_scene)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentBufferIndex = a_context.bufferIndex;
            ECSManager::System<TransformComponent, ParticleEmitterComponent>::update(
                a_context);
            m_currentBufferIndex = 0;
        }

    private:
        static constexpr uint32_t k_invalidParticleBase =
            (std::numeric_limits<uint32_t>::max)();
        static constexpr uint32_t k_maxSpawnPerFrame = 64;

        [[nodiscard]] uint32_t allocate_particle_range(
            ParticleEmitterComponent& a_emitter)
        {
            if (a_emitter.runtimeParticleBase != k_invalidParticleBase &&
                a_emitter.runtimeParticleCapacity > 0)
            {
                return a_emitter.runtimeParticleCapacity;
            }

            const uint32_t requestedCapacity = (std::min)(
                (std::max)(a_emitter.maxParticleCount, 1u),
                GpuData::k_defaultEmitterParticleCapacity);
            if (m_nextParticleBase + requestedCapacity > GpuData::k_maxParticleCount)
            {
                return 0;
            }

            a_emitter.runtimeParticleBase = m_nextParticleBase;
            a_emitter.runtimeParticleCapacity = requestedCapacity;
            m_nextParticleBase += requestedCapacity;
            return requestedCapacity;
        }

        [[nodiscard]] uint32_t calculate_spawn_count(
            ParticleEmitterComponent& a_emitter,
            float a_deltaTime)
        {
            if (!a_emitter.isPlaying || a_deltaTime <= 0.0f)
            {
                return 0;
            }

            uint32_t spawnCount = a_emitter.burstCount;
            a_emitter.burstCount = 0;

            if (a_emitter.emitRate > 0.0f)
            {
                a_emitter.runtimeEmitAccumulator +=
                    a_emitter.emitRate * a_deltaTime;
                const uint32_t continuousCount =
                    static_cast<uint32_t>(a_emitter.runtimeEmitAccumulator);
                if (continuousCount > 0)
                {
                    spawnCount += continuousCount;
                    a_emitter.runtimeEmitAccumulator -=
                        static_cast<float>(continuousCount);
                }
            }

            return (std::min)(spawnCount, k_maxSpawnPerFrame);
        }

        void collect_emitter(Entity a_entity,
            TransformComponent& a_transform,
            ParticleEmitterComponent& a_emitter,
            const UpdateContext& a_context)
        {
            a_entity;
            if (!a_emitter.isVisible)
            {
                return;
            }

            const uint32_t capacity = allocate_particle_range(a_emitter);
            if (capacity == 0)
            {
                return;
            }

            Math::float4 materialColor(1.0f, 1.0f, 1.0f, 1.0f);
            uint32_t textureId = 0;
            uint32_t useTexture = 0;
            const MaterialHandle materialHandle =
                a_emitter.materialHandle.valid()
                ? a_emitter.materialHandle
                : m_defaultMaterialHandle;
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

            const uint32_t spawnCount =
                calculate_spawn_count(a_emitter, a_context.deltaTime);

            ParticleSystem::ParticleEmitterItem item{};
            item.emitter.position = a_transform.position;
            item.emitter.particleBase = a_emitter.runtimeParticleBase;
            item.emitter.velocityMin = a_emitter.velocityMin;
            item.emitter.particleCapacity = capacity;
            item.emitter.velocityMax = a_emitter.velocityMax;
            item.emitter.spawnCount = spawnCount;
            item.emitter.acceleration = a_emitter.acceleration;
            item.emitter.spawnCursor = a_emitter.runtimeSpawnCursor;
            item.emitter.startColor = Math::float4(
                a_emitter.startColor.r * materialColor.r,
                a_emitter.startColor.g * materialColor.g,
                a_emitter.startColor.b * materialColor.b,
                a_emitter.startColor.a * materialColor.a);
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
            item.emitter.textureId = textureId;
            item.emitter.useTexture = useTexture;
            item.emitter.randomSeed = a_emitter.randomSeed;
            item.emitter.billboardMode =
                static_cast<uint32_t>(a_emitter.billboardMode);

            if (m_scene.submit_emitter(m_currentBufferIndex, item) &&
                spawnCount > 0)
            {
                a_emitter.runtimeSpawnCursor =
                    (a_emitter.runtimeSpawnCursor + spawnCount) % capacity;
            }
        }

        AssetManager* m_assetManager = nullptr;
        MaterialHandle m_defaultMaterialHandle{};
        ParticleSystem::ParticleScene& m_scene;
        uint32_t m_currentBufferIndex = 0;
        uint32_t m_nextParticleBase = 0;
    };
} // namespace Cue::ECS
