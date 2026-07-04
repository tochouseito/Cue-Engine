#pragma once

/// ****************************************************************************
/// ParticleEffectComponent を DrawScene へ変換する System
/// ****************************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "DrawSystem/DrawScene.h"
#include "GameCore/Components.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

namespace Cue::ECS
{
    class EffectSystem final : public ECSManager::System<WorldTransformComponent, ParticleEffectComponent>
    {
    public:
        explicit EffectSystem(std::vector<DrawSystem::DrawScene>& a_drawScenes)
            : ECSManager::System<WorldTransformComponent, ParticleEffectComponent>(
                  [this](Entity a_entity,
                         WorldTransformComponent& a_transform,
                         ParticleEffectComponent& a_effect,
                         const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_transform, a_effect, a_context);
                  })
            , m_drawScenes(a_drawScenes)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            if (a_context.bufferIndex >= m_drawScenes.size())
            {
                return;
            }

            ECSManager::System<WorldTransformComponent, ParticleEffectComponent>::update(a_context);
        }

    private:
        struct EmitterRuntime final
        {
            EffectNode* emitter = nullptr;
            EffectNode* generation = nullptr;
            EffectNode* lifetime = nullptr;
            EffectNode* position = nullptr;
            EffectNode* velocity = nullptr;
            EffectNode* color = nullptr;
            EffectNode* renderer = nullptr;
            int32_t emitterNodeIndex = -1;
        };

        void update_component(Entity,
                              const WorldTransformComponent& a_transform,
                              ParticleEffectComponent& a_effect,
                              const UpdateContext& a_context)
        {
            const float deltaTime = std::max(a_context.deltaTime, 0.0f);
            ensure_effect_nodes(a_effect);
            apply_effect_nodes_to_component(a_effect);

            if (!is_node_enabled(a_effect, EffectNodeKind::Root))
            {
                a_effect.particles.clear();
                return;
            }

            const uint32_t maxParticles = max_particle_count(a_effect);
            if (maxParticles == 0)
            {
                a_effect.particles.clear();
                return;
            }

            if (a_effect.particles.size() > maxParticles)
            {
                a_effect.particles.resize(maxParticles);
            }

            update_particles(a_effect, deltaTime);
            for (int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>(a_effect.nodes.size()); ++nodeIndex)
            {
                if (a_effect.nodes[static_cast<size_t>(nodeIndex)].kind == EffectNodeKind::Emitter)
                {
                    spawn_particles(a_transform, a_effect, nodeIndex, deltaTime);
                }
            }
            append_draw_particles(a_context.bufferIndex, a_effect);
        }

        static bool is_node_enabled(const ParticleEffectComponent& a_effect, EffectNodeKind a_kind) noexcept
        {
            const EffectNode* node = find_effect_node(a_effect, a_kind);
            return node == nullptr || node->isEnabled;
        }

        static void update_particles(ParticleEffectComponent& a_effect, float a_deltaTime)
        {
            for (ParticleInstance& particle : a_effect.particles)
            {
                particle.age += a_deltaTime;
                particle.velocity += particle.acceleration * a_deltaTime;
                particle.position += particle.velocity * a_deltaTime;
            }

            a_effect.particles.erase(
                std::remove_if(
                    a_effect.particles.begin(),
                    a_effect.particles.end(),
                    [](const ParticleInstance& a_particle)
                    {
                        return a_particle.age >= a_particle.lifetime;
                    }),
                a_effect.particles.end());
        }

        static uint32_t max_particle_count(const ParticleEffectComponent& a_effect) noexcept
        {
            uint32_t maxParticles = 0;
            for (int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>(a_effect.nodes.size()); ++nodeIndex)
            {
                const EffectNode& node = a_effect.nodes[static_cast<size_t>(nodeIndex)];
                if (node.kind != EffectNodeKind::Emitter || !node.isEnabled)
                {
                    continue;
                }

                const EffectNode* generation =
                    find_effect_child_node(a_effect, nodeIndex, EffectNodeKind::Generation);
                const EffectNode* lifetime =
                    find_effect_child_node(a_effect, nodeIndex, EffectNodeKind::Lifetime);
                const EffectNode* renderer =
                    find_effect_child_node(a_effect, nodeIndex, EffectNodeKind::Renderer);
                if (generation == nullptr || lifetime == nullptr || renderer == nullptr ||
                    !generation->isEnabled || !lifetime->isEnabled || !renderer->isEnabled)
                {
                    continue;
                }

                maxParticles += generation->maxParticles;
            }

            return maxParticles;
        }

        static EmitterRuntime resolve_emitter(ParticleEffectComponent& a_effect,
                                              int32_t a_emitterNodeIndex) noexcept
        {
            EmitterRuntime emitter{};
            if (a_emitterNodeIndex < 0 || a_emitterNodeIndex >= static_cast<int32_t>(a_effect.nodes.size()))
            {
                return emitter;
            }

            emitter.emitter = &a_effect.nodes[static_cast<size_t>(a_emitterNodeIndex)];
            emitter.emitterNodeIndex = a_emitterNodeIndex;
            emitter.generation = find_effect_child_node(a_effect, a_emitterNodeIndex, EffectNodeKind::Generation);
            emitter.lifetime = find_effect_child_node(a_effect, a_emitterNodeIndex, EffectNodeKind::Lifetime);
            emitter.position = find_effect_child_node(a_effect, a_emitterNodeIndex, EffectNodeKind::Position);
            emitter.velocity = find_effect_child_node(a_effect, a_emitterNodeIndex, EffectNodeKind::Velocity);
            emitter.color = find_effect_child_node(a_effect, a_emitterNodeIndex, EffectNodeKind::Color);
            emitter.renderer = find_effect_child_node(a_effect, a_emitterNodeIndex, EffectNodeKind::Renderer);
            return emitter;
        }

        static bool can_spawn(const EmitterRuntime& a_emitter) noexcept
        {
            return a_emitter.emitter != nullptr && a_emitter.generation != nullptr &&
                a_emitter.lifetime != nullptr && a_emitter.velocity != nullptr &&
                a_emitter.color != nullptr && a_emitter.renderer != nullptr &&
                a_emitter.emitter->isEnabled && a_emitter.generation->isEnabled &&
                a_emitter.lifetime->isEnabled && a_emitter.velocity->isEnabled &&
                a_emitter.color->isEnabled && a_emitter.renderer->isEnabled;
        }

        static uint32_t particle_count_for_emitter(const ParticleEffectComponent& a_effect,
                                                   int32_t a_emitterNodeIndex) noexcept
        {
            uint32_t count = 0;
            for (const ParticleInstance& particle : a_effect.particles)
            {
                if (particle.emitterNodeIndex == a_emitterNodeIndex)
                {
                    ++count;
                }
            }

            return count;
        }

        static void spawn_particles(const WorldTransformComponent& a_transform,
                                    ParticleEffectComponent& a_effect,
                                    int32_t a_emitterNodeIndex,
                                    float a_deltaTime)
        {
            EmitterRuntime emitter = resolve_emitter(a_effect, a_emitterNodeIndex);
            if (!can_spawn(emitter) || !a_effect.isPlaying ||
                emitter.generation->spawnRate <= 0.0f ||
                emitter.lifetime->particleLifetime <= 0.0f)
            {
                return;
            }

            emitter.generation->spawnAccumulator += emitter.generation->spawnRate * a_deltaTime;
            uint32_t spawnCount = static_cast<uint32_t>(emitter.generation->spawnAccumulator);
            emitter.generation->spawnAccumulator -= static_cast<float>(spawnCount);

            const uint32_t emitterParticleCount = particle_count_for_emitter(a_effect, a_emitterNodeIndex);
            const uint32_t availableCount = emitter.generation->maxParticles > emitterParticleCount
                ? emitter.generation->maxParticles - emitterParticleCount
                : 0u;
            spawnCount = std::min(spawnCount, availableCount);

            for (uint32_t particleIndex = 0; particleIndex < spawnCount; ++particleIndex)
            {
                ParticleInstance particle{};
                const Math::float3 positionOffset = emitter.position != nullptr && emitter.position->isEnabled
                    ? emitter.position->position
                    : Math::float3::zero();
                particle.position = a_transform.position + emitter.emitter->position + positionOffset;
                particle.velocity = emitter.velocity->initialVelocity + random_spread(a_effect, *emitter.velocity);
                particle.acceleration = emitter.velocity->acceleration;
                particle.startColor = emitter.color->startColor;
                particle.endColor = emitter.color->endColor;
                particle.lifetime = emitter.lifetime->particleLifetime;
                particle.startSize = emitter.renderer->startSize;
                particle.endSize = emitter.renderer->endSize;
                particle.emitterNodeIndex = a_emitterNodeIndex;

                try
                {
                    a_effect.particles.push_back(particle);
                }
                catch (const std::bad_alloc&)
                {
                    return;
                }
            }

            if (!a_effect.isLooping && a_effect.particles.empty())
            {
                a_effect.isPlaying = false;
            }
        }

        static Math::float3 random_spread(ParticleEffectComponent& a_effect, const EffectNode& a_velocityNode) noexcept
        {
            return Math::float3(
                next_signed_random(a_effect.randomSeed) * a_velocityNode.velocitySpread.x,
                next_signed_random(a_effect.randomSeed) * a_velocityNode.velocitySpread.y,
                next_signed_random(a_effect.randomSeed) * a_velocityNode.velocitySpread.z);
        }

        static float next_signed_random(uint32_t& a_seed) noexcept
        {
            a_seed = a_seed * 1664525u + 1013904223u;
            const uint32_t value = (a_seed >> 8u) & 0x00ffffffu;
            const float normalized = static_cast<float>(value) / static_cast<float>(0x00ffffffu);
            return normalized * 2.0f - 1.0f;
        }

        void append_draw_particles(uint32_t a_bufferIndex, const ParticleEffectComponent& a_effect)
        {
            DrawSystem::DrawScene& drawScene = m_drawScenes[a_bufferIndex];
            for (const ParticleInstance& particle : a_effect.particles)
            {
                const float normalizedLife =
                    particle.lifetime > 0.0f ? std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f) : 1.0f;

                GpuData::ParticleSpriteGpu gpu{};
                const float size = particle.startSize + (particle.endSize - particle.startSize) * normalizedLife;
                gpu.positionSize = Math::float4(particle.position.x, particle.position.y, particle.position.z, size);
                gpu.color = particle.startColor + (particle.endColor - particle.startColor) * normalizedLife;

                const Result result = drawScene.add_particle_sprite(gpu);
                CUE_ASSERT_FORMAT(success(result), "Failed to add particle sprite to DrawScene: {}", result.message.data());
            }
        }

        std::vector<DrawSystem::DrawScene>& m_drawScenes;
    };
} // namespace Cue::ECS
