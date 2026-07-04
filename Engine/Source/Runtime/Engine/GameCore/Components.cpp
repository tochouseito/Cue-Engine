#include "Components.h"

// === C++ includes ===
#include <algorithm>
#include <string>
#include <utility>

namespace Cue::ECS
{
    namespace
    {
        EffectNode make_node(std::string a_name,
                             EffectNodeKind a_kind,
                             int32_t a_parentIndex,
                             const ParticleEffectComponent& a_effect)
        {
            EffectNode node{};
            node.name = std::move(a_name);
            node.kind = a_kind;
            node.parentIndex = a_parentIndex;
            node.initialVelocity = a_effect.initialVelocity;
            node.velocitySpread = a_effect.velocitySpread;
            node.acceleration = a_effect.acceleration;
            node.startColor = a_effect.startColor;
            node.endColor = a_effect.endColor;
            node.spawnRate = a_effect.spawnRate;
            node.particleLifetime = a_effect.particleLifetime;
            node.startSize = a_effect.startSize;
            node.endSize = a_effect.endSize;
            node.maxParticles = a_effect.maxParticles;
            node.isAdditive = a_effect.isAdditive;
            return node;
        }

        void add_effect_emitter_children(ParticleEffectComponent& a_effect, int32_t a_emitterNodeIndex)
        {
            a_effect.nodes.push_back(make_node("Generation", EffectNodeKind::Generation, a_emitterNodeIndex, a_effect));
            a_effect.nodes.push_back(make_node("Lifetime", EffectNodeKind::Lifetime, a_emitterNodeIndex, a_effect));
            a_effect.nodes.push_back(make_node("Position", EffectNodeKind::Position, a_emitterNodeIndex, a_effect));
            a_effect.nodes.push_back(make_node("Velocity", EffectNodeKind::Velocity, a_emitterNodeIndex, a_effect));
            a_effect.nodes.push_back(make_node("Color", EffectNodeKind::Color, a_emitterNodeIndex, a_effect));
            a_effect.nodes.push_back(make_node("Renderer", EffectNodeKind::Renderer, a_emitterNodeIndex, a_effect));
            a_effect.nodes.push_back(make_node("Statistics", EffectNodeKind::Statistics, a_emitterNodeIndex, a_effect));
        }
    } // namespace

    RenderableInfoComponent::RenderableInfoComponent() = default;

    RenderableInfoComponent::RenderableInfoComponent(const RenderableInfoComponent&) = default;

    RenderableInfoComponent& RenderableInfoComponent::operator=(const RenderableInfoComponent&) = default;

    RenderableInfoComponent::RenderableInfoComponent(RenderableInfoComponent&&) = default;

    RenderableInfoComponent& RenderableInfoComponent::operator=(RenderableInfoComponent&&) = default;

    TransformComponent::TransformComponent() = default;

    TransformComponent::TransformComponent(const TransformComponent&) = default;

    TransformComponent& TransformComponent::operator=(const TransformComponent&) = default;

    TransformComponent::TransformComponent(TransformComponent&&) = default;

    TransformComponent& TransformComponent::operator=(TransformComponent&&) = default;

    WorldTransformComponent::WorldTransformComponent() = default;

    WorldTransformComponent::WorldTransformComponent(const WorldTransformComponent&) = default;

    WorldTransformComponent& WorldTransformComponent::operator=(const WorldTransformComponent&) = default;

    WorldTransformComponent::WorldTransformComponent(WorldTransformComponent&&) = default;

    WorldTransformComponent& WorldTransformComponent::operator=(WorldTransformComponent&&) = default;

    CameraComponent::CameraComponent() = default;

    CameraComponent::CameraComponent(const CameraComponent&) = default;

    CameraComponent& CameraComponent::operator=(const CameraComponent&) = default;

    CameraComponent::CameraComponent(CameraComponent&&) = default;

    CameraComponent& CameraComponent::operator=(CameraComponent&&) = default;

    MeshFilterComponent::MeshFilterComponent() = default;

    MeshFilterComponent::MeshFilterComponent(const MeshFilterComponent&) = default;

    MeshFilterComponent& MeshFilterComponent::operator=(const MeshFilterComponent&) = default;

    MeshFilterComponent::MeshFilterComponent(MeshFilterComponent&&) = default;

    MeshFilterComponent& MeshFilterComponent::operator=(MeshFilterComponent&&) = default;

    ParticleEffectComponent::ParticleEffectComponent()
    {
        reset_effect_nodes_from_component(*this);
    }

    ParticleEffectComponent::ParticleEffectComponent(const ParticleEffectComponent&) = default;

    ParticleEffectComponent& ParticleEffectComponent::operator=(const ParticleEffectComponent&) = default;

    ParticleEffectComponent::ParticleEffectComponent(ParticleEffectComponent&&) = default;

    ParticleEffectComponent& ParticleEffectComponent::operator=(ParticleEffectComponent&&) = default;

    void reset_effect_nodes_from_component(ParticleEffectComponent& a_effect)
    {
        a_effect.nodes.clear();
        a_effect.nodes.push_back(make_node("Root", EffectNodeKind::Root, -1, a_effect));
        a_effect.nodes.push_back(make_node("Emitter", EffectNodeKind::Emitter, 0, a_effect));
        add_effect_emitter_children(a_effect, 1);
    }

    void ensure_effect_nodes(ParticleEffectComponent& a_effect)
    {
        if (a_effect.nodes.empty())
        {
            reset_effect_nodes_from_component(a_effect);
        }
    }

    void apply_effect_nodes_to_component(ParticleEffectComponent& a_effect)
    {
        ensure_effect_nodes(a_effect);

        const EffectNode* emitter = find_effect_node(a_effect, EffectNodeKind::Emitter);
        const int32_t emitterIndex = emitter != nullptr
            ? static_cast<int32_t>(emitter - a_effect.nodes.data())
            : -1;
        if (emitterIndex < 0)
        {
            return;
        }

        if (const EffectNode* generation = find_effect_child_node(a_effect, emitterIndex, EffectNodeKind::Generation);
            generation != nullptr && generation->isEnabled)
        {
            a_effect.spawnRate = generation->spawnRate;
            a_effect.maxParticles = generation->maxParticles;
        }

        if (const EffectNode* lifetime = find_effect_child_node(a_effect, emitterIndex, EffectNodeKind::Lifetime);
            lifetime != nullptr && lifetime->isEnabled)
        {
            a_effect.particleLifetime = lifetime->particleLifetime;
        }

        if (const EffectNode* velocity = find_effect_child_node(a_effect, emitterIndex, EffectNodeKind::Velocity);
            velocity != nullptr && velocity->isEnabled)
        {
            a_effect.initialVelocity = velocity->initialVelocity;
            a_effect.velocitySpread = velocity->velocitySpread;
            a_effect.acceleration = velocity->acceleration;
        }

        if (const EffectNode* color = find_effect_child_node(a_effect, emitterIndex, EffectNodeKind::Color);
            color != nullptr && color->isEnabled)
        {
            a_effect.startColor = color->startColor;
            a_effect.endColor = color->endColor;
        }

        if (const EffectNode* renderer = find_effect_child_node(a_effect, emitterIndex, EffectNodeKind::Renderer);
            renderer != nullptr && renderer->isEnabled)
        {
            a_effect.startSize = renderer->startSize;
            a_effect.endSize = renderer->endSize;
            a_effect.isAdditive = renderer->isAdditive;
        }
    }

    int32_t add_effect_emitter_node(ParticleEffectComponent& a_effect)
    {
        ensure_effect_nodes(a_effect);

        uint32_t emitterCount = 0;
        for (const EffectNode& node : a_effect.nodes)
        {
            if (node.kind == EffectNodeKind::Emitter)
            {
                ++emitterCount;
            }
        }

        EffectNode emitter = make_node("Emitter " + std::to_string(emitterCount + 1u),
                                       EffectNodeKind::Emitter,
                                       0,
                                       a_effect);
        emitter.position = Math::float3(static_cast<float>(emitterCount) * 0.35f, 0.0f, 0.0f);
        a_effect.nodes.push_back(std::move(emitter));
        const int32_t emitterNodeIndex = static_cast<int32_t>(a_effect.nodes.size() - 1u);
        add_effect_emitter_children(a_effect, emitterNodeIndex);
        return emitterNodeIndex;
    }

    void remove_effect_emitter_node(ParticleEffectComponent& a_effect, int32_t a_emitterNodeIndex)
    {
        if (a_emitterNodeIndex <= 1 || a_emitterNodeIndex >= static_cast<int32_t>(a_effect.nodes.size()))
        {
            return;
        }
        if (a_effect.nodes[static_cast<size_t>(a_emitterNodeIndex)].kind != EffectNodeKind::Emitter)
        {
            return;
        }

        std::vector<int32_t> indexMap(a_effect.nodes.size(), -1);
        std::vector<EffectNode> newNodes{};
        newNodes.reserve(a_effect.nodes.size());
        for (int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>(a_effect.nodes.size()); ++nodeIndex)
        {
            const EffectNode& node = a_effect.nodes[static_cast<size_t>(nodeIndex)];
            if (nodeIndex == a_emitterNodeIndex || node.parentIndex == a_emitterNodeIndex)
            {
                continue;
            }
            indexMap[static_cast<size_t>(nodeIndex)] = static_cast<int32_t>(newNodes.size());
            newNodes.push_back(node);
        }

        for (EffectNode& node : newNodes)
        {
            if (node.parentIndex >= 0 && node.parentIndex < static_cast<int32_t>(indexMap.size()))
            {
                node.parentIndex = indexMap[static_cast<size_t>(node.parentIndex)];
            }
        }

        a_effect.nodes = std::move(newNodes);
        a_effect.particles.clear();
    }

    EffectNode* find_effect_node(ParticleEffectComponent& a_effect, EffectNodeKind a_kind) noexcept
    {
        for (EffectNode& node : a_effect.nodes)
        {
            if (node.kind == a_kind)
            {
                return &node;
            }
        }

        return nullptr;
    }

    const EffectNode* find_effect_node(const ParticleEffectComponent& a_effect, EffectNodeKind a_kind) noexcept
    {
        for (const EffectNode& node : a_effect.nodes)
        {
            if (node.kind == a_kind)
            {
                return &node;
            }
        }

        return nullptr;
    }

    EffectNode* find_effect_child_node(ParticleEffectComponent& a_effect,
                                       int32_t a_parentIndex,
                                       EffectNodeKind a_kind) noexcept
    {
        for (EffectNode& node : a_effect.nodes)
        {
            if (node.parentIndex == a_parentIndex && node.kind == a_kind)
            {
                return &node;
            }
        }

        return nullptr;
    }

    const EffectNode* find_effect_child_node(const ParticleEffectComponent& a_effect,
                                             int32_t a_parentIndex,
                                             EffectNodeKind a_kind) noexcept
    {
        for (const EffectNode& node : a_effect.nodes)
        {
            if (node.parentIndex == a_parentIndex && node.kind == a_kind)
            {
                return &node;
            }
        }

        return nullptr;
    }

    StaticMeshRendererComponent::StaticMeshRendererComponent() = default;

    StaticMeshRendererComponent::StaticMeshRendererComponent(const StaticMeshRendererComponent&) = default;

    StaticMeshRendererComponent& StaticMeshRendererComponent::operator=(const StaticMeshRendererComponent&) = default;

    StaticMeshRendererComponent::StaticMeshRendererComponent(StaticMeshRendererComponent&&) = default;

    StaticMeshRendererComponent& StaticMeshRendererComponent::operator=(StaticMeshRendererComponent&&) = default;
} // namespace Cue::ECS
