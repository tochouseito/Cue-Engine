#include "EffectEditor.h"

// === Runtime includes ===
#include <GameCore/Components.h>
#include <GameCore/GameObject.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <string>

// === ImGui includes ===
#include <imgui.h>

namespace
{
    constexpr const char* k_defaultEffectName = "TestEffect";
    constexpr float k_nodeTreeWidth = 230.0f;

    void drag_float3(const char* a_label, Cue::Math::float3& a_value, float a_speed)
    {
        float values[3] = {a_value.x, a_value.y, a_value.z};
        if (ImGui::DragFloat3(a_label, values, a_speed))
        {
            a_value = Cue::Math::float3(values[0], values[1], values[2]);
        }
    }

    void read_float3(const char* a_label, const Cue::Math::float3& a_value)
    {
        ImGui::Text("%s: %.3f, %.3f, %.3f", a_label, a_value.x, a_value.y, a_value.z);
    }

    void color_edit4(const char* a_label, Cue::Math::float4& a_value)
    {
        float values[4] = {a_value.x, a_value.y, a_value.z, a_value.w};
        if (ImGui::ColorEdit4(a_label, values))
        {
            a_value = Cue::Math::float4(values[0], values[1], values[2], values[3]);
        }
    }

    void restart_effect(Cue::ECS::ParticleEffectComponent& a_effect)
    {
        a_effect.particles.clear();
        a_effect.spawnAccumulator = 0.0f;
        for (Cue::ECS::EffectNode& node : a_effect.nodes)
        {
            node.spawnAccumulator = 0.0f;
        }
        a_effect.randomSeed = 1u;
        a_effect.isPlaying = true;
    }

    void stop_effect(Cue::ECS::ParticleEffectComponent& a_effect)
    {
        a_effect.particles.clear();
        a_effect.spawnAccumulator = 0.0f;
        for (Cue::ECS::EffectNode& node : a_effect.nodes)
        {
            node.spawnAccumulator = 0.0f;
        }
        a_effect.isPlaying = false;
    }

    void reset_seed(Cue::ECS::ParticleEffectComponent& a_effect)
    {
        a_effect.randomSeed = 1u;
    }

    void clamp_particle_capacity(Cue::ECS::ParticleEffectComponent& a_effect)
    {
        if (a_effect.particles.size() > a_effect.maxParticles)
        {
            a_effect.particles.resize(a_effect.maxParticles);
        }
    }

    void drag_node_capacity(Cue::ECS::EffectNode& a_node)
    {
        int maxParticles = static_cast<int>(a_node.maxParticles);
        if (ImGui::DragInt("Max Particles", &maxParticles, 1.0f, 0, 8192))
        {
            maxParticles = std::clamp(maxParticles, 0, 8192);
            a_node.maxParticles = static_cast<uint32_t>(maxParticles);
        }
    }

    void drag_random_seed(Cue::ECS::ParticleEffectComponent& a_effect)
    {
        int randomSeed = static_cast<int>(a_effect.randomSeed);
        if (ImGui::DragInt("Random Seed", &randomSeed, 1.0f, 1, 2147483647))
        {
            randomSeed = std::max(randomSeed, 1);
            a_effect.randomSeed = static_cast<uint32_t>(randomSeed);
        }
    }
} // namespace

namespace Cue::Editor
{
    EffectEditor::EffectEditor(GameCore::GameWorld* a_gameWorld,
                               GameCore::EntityId* a_selectedEntityId) noexcept
        : m_gameWorld(a_gameWorld),
          m_selectedEntityId(a_selectedEntityId)
    {
    }

    EffectEditor::~EffectEditor() = default;

    void EffectEditor::set_game_world(GameCore::GameWorld* a_gameWorld) noexcept
    {
        m_gameWorld = a_gameWorld;
    }

    void EffectEditor::update()
    {
        ImGui::Begin("Effect Editor");

        if (m_gameWorld == nullptr || m_selectedEntityId == nullptr)
        {
            ImGui::TextUnformatted("Effect editor の依存が初期化されていません。");
            ImGui::End();
            return;
        }

        GameCore::GameObject object{};
        ECS::ParticleEffectComponent* effect = nullptr;
        ECS::TransformComponent* transform = nullptr;
        ECS::WorldTransformComponent* worldTransform = nullptr;
        if (!find_effect_object(object, effect, transform, worldTransform) || effect == nullptr)
        {
            ImGui::TextUnformatted("ParticleEffectComponent を持つ GameObject がありません。");
            ImGui::End();
            return;
        }
        ECS::ensure_effect_nodes(*effect);
        if (m_selectedNodeIndex < 0 || m_selectedNodeIndex >= static_cast<int32_t>(effect->nodes.size()))
        {
            m_selectedNodeIndex = effect->nodes.size() > 1 ? 1 : 0;
        }

        draw_toolbar(*effect);
        ImGui::Separator();

        ImGui::BeginChild("NodeTree", ImVec2(k_nodeTreeWidth, 0.0f), true);
        draw_node_tree(*effect);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("NodeProperties", ImVec2(0.0f, 0.0f), true);
        draw_node_properties(object, *effect, transform, worldTransform);
        ImGui::EndChild();

        ImGui::End();
    }

    bool EffectEditor::find_effect_object(GameCore::GameObject& a_outObject,
                                          ECS::ParticleEffectComponent*& a_outEffect,
                                          ECS::TransformComponent*& a_outTransform,
                                          ECS::WorldTransformComponent*& a_outWorldTransform)
    {
        GameCore::GameObject selected{};
        if (find_selected_object(selected) &&
            try_get_effect(selected, a_outEffect, a_outTransform, a_outWorldTransform))
        {
            a_outObject = selected;
            return true;
        }

        GameCore::GameObject defaultEffect{};
        if (!m_gameWorld->find_object_by_name(k_defaultEffectName, defaultEffect) || !defaultEffect.is_valid())
        {
            return false;
        }

        if (!try_get_effect(defaultEffect, a_outEffect, a_outTransform, a_outWorldTransform))
        {
            return false;
        }

        a_outObject = defaultEffect;
        return true;
    }

    bool EffectEditor::find_selected_object(GameCore::GameObject& a_outObject)
    {
        if (m_gameWorld == nullptr || m_selectedEntityId == nullptr ||
            *m_selectedEntityId == GameCore::k_invalidEntityId)
        {
            return false;
        }

        bool found = false;
        const GameCore::EntityId selectedEntityId = *m_selectedEntityId;
        const Result result =
            m_gameWorld->for_each_object(
                [&a_outObject, &found, selectedEntityId](
                    GameCore::EntityId a_entityId,
                    GameCore::GameObject a_object)
                {
                    if (found || a_entityId != selectedEntityId)
                    {
                        return;
                    }

                    a_outObject = a_object;
                    found = a_object.is_valid();
                });

        return result && found;
    }

    bool EffectEditor::try_get_effect(GameCore::GameObject& a_object,
                                      ECS::ParticleEffectComponent*& a_outEffect,
                                      ECS::TransformComponent*& a_outTransform,
                                      ECS::WorldTransformComponent*& a_outWorldTransform)
    {
        a_outEffect = nullptr;
        a_outTransform = nullptr;
        a_outWorldTransform = nullptr;

        if (!a_object.get_component(a_outEffect) || a_outEffect == nullptr)
        {
            return false;
        }

        (void)a_object.get_component(a_outTransform);
        (void)a_object.get_component(a_outWorldTransform);
        return true;
    }

    void EffectEditor::draw_toolbar(ECS::ParticleEffectComponent& a_effect)
    {
        if (ImGui::Button(a_effect.isPlaying ? "Pause" : "Play"))
        {
            a_effect.isPlaying = !a_effect.isPlaying;
        }

        ImGui::SameLine();
        if (ImGui::Button("Restart"))
        {
            restart_effect(a_effect);
        }

        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            stop_effect(a_effect);
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset Seed"))
        {
            reset_seed(a_effect);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Loop", &a_effect.isLooping);

        ImGui::SameLine();
        ImGui::Text("Alive %zu / %u", a_effect.particles.size(), a_effect.maxParticles);
    }

    const char* EffectEditor::node_kind_name(ECS::EffectNodeKind a_kind) const noexcept
    {
        switch (a_kind)
        {
        case ECS::EffectNodeKind::Root:
            return "Root";
        case ECS::EffectNodeKind::Emitter:
            return "Emitter";
        case ECS::EffectNodeKind::Generation:
            return "Generation";
        case ECS::EffectNodeKind::Lifetime:
            return "Lifetime";
        case ECS::EffectNodeKind::Position:
            return "Position";
        case ECS::EffectNodeKind::Velocity:
            return "Velocity";
        case ECS::EffectNodeKind::Color:
            return "Color";
        case ECS::EffectNodeKind::Renderer:
            return "Renderer";
        case ECS::EffectNodeKind::Statistics:
            return "Statistics";
        default:
            return "Node";
        }
    }

    void EffectEditor::draw_node_tree(ECS::ParticleEffectComponent& a_effect)
    {
        ImGui::TextUnformatted("Nodes");
        if (ImGui::Button("Add Emitter"))
        {
            m_selectedNodeIndex = ECS::add_effect_emitter_node(a_effect);
        }

        if (m_selectedNodeIndex > 1 && m_selectedNodeIndex < static_cast<int32_t>(a_effect.nodes.size()) &&
            a_effect.nodes[static_cast<size_t>(m_selectedNodeIndex)].kind == ECS::EffectNodeKind::Emitter)
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete Emitter"))
            {
                ECS::remove_effect_emitter_node(a_effect, m_selectedNodeIndex);
                m_selectedNodeIndex = a_effect.nodes.size() > 1 ? 1 : 0;
                return;
            }
        }

        ImGui::Separator();

        for (int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>(a_effect.nodes.size()); ++nodeIndex)
        {
            if (a_effect.nodes[static_cast<size_t>(nodeIndex)].parentIndex == -1)
            {
                draw_node_item(a_effect, nodeIndex);
            }
        }
    }

    void EffectEditor::draw_node_item(ECS::ParticleEffectComponent& a_effect, int32_t a_nodeIndex)
    {
        if (a_nodeIndex < 0 || a_nodeIndex >= static_cast<int32_t>(a_effect.nodes.size()))
        {
            return;
        }

        bool hasChild = false;
        for (const ECS::EffectNode& node : a_effect.nodes)
        {
            hasChild = hasChild || node.parentIndex == a_nodeIndex;
        }

        const ECS::EffectNode& node = a_effect.nodes[static_cast<size_t>(a_nodeIndex)];
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            (m_selectedNodeIndex == a_nodeIndex ? ImGuiTreeNodeFlags_Selected : 0);
        if (!hasChild)
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (node.kind == ECS::EffectNodeKind::Root || node.kind == ECS::EffectNodeKind::Emitter)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        const bool isOpen = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(static_cast<intptr_t>(a_nodeIndex)),
            flags,
            "%s%s",
            node.isEnabled ? "" : "[x] ",
            node.name.c_str());
        if (ImGui::IsItemClicked())
        {
            m_selectedNodeIndex = a_nodeIndex;
        }

        if (hasChild && isOpen)
        {
            for (int32_t childIndex = 0; childIndex < static_cast<int32_t>(a_effect.nodes.size()); ++childIndex)
            {
                if (a_effect.nodes[static_cast<size_t>(childIndex)].parentIndex == a_nodeIndex)
                {
                    draw_node_item(a_effect, childIndex);
                }
            }
            ImGui::TreePop();
        }
    }

    void EffectEditor::draw_node_properties(const GameCore::GameObject& a_object,
                                            ECS::ParticleEffectComponent& a_effect,
                                            ECS::TransformComponent* a_transform,
                                            ECS::WorldTransformComponent* a_worldTransform)
    {
        if (m_selectedNodeIndex < 0 || m_selectedNodeIndex >= static_cast<int32_t>(a_effect.nodes.size()))
        {
            return;
        }

        ECS::EffectNode& selectedNode = a_effect.nodes[static_cast<size_t>(m_selectedNodeIndex)];
        ImGui::Text("Node: %s", selectedNode.name.c_str());
        ImGui::Separator();
        ImGui::Checkbox("Enabled", &selectedNode.isEnabled);
        ImGui::Text("Kind: %s", node_kind_name(selectedNode.kind));
        ImGui::Text("Parent: %d", selectedNode.parentIndex);
        ImGui::Separator();

        switch (selectedNode.kind)
        {
        case ECS::EffectNodeKind::Root:
            draw_root_properties(a_object, a_effect, a_transform, a_worldTransform);
            break;
        case ECS::EffectNodeKind::Emitter:
            draw_emitter_properties(a_effect, m_selectedNodeIndex);
            break;
        case ECS::EffectNodeKind::Generation:
            draw_generation_properties(a_effect, selectedNode);
            break;
        case ECS::EffectNodeKind::Lifetime:
            draw_lifetime_properties(a_effect, selectedNode);
            break;
        case ECS::EffectNodeKind::Position:
            draw_position_properties(selectedNode);
            break;
        case ECS::EffectNodeKind::Velocity:
            draw_velocity_properties(selectedNode);
            break;
        case ECS::EffectNodeKind::Color:
            draw_color_properties(selectedNode);
            break;
        case ECS::EffectNodeKind::Renderer:
            draw_renderer_properties(selectedNode);
            break;
        case ECS::EffectNodeKind::Statistics:
            draw_statistics_properties(a_object, a_effect);
            break;
        default:
            break;
        }

        ECS::apply_effect_nodes_to_component(a_effect);
        clamp_particle_capacity(a_effect);
    }

    void EffectEditor::draw_root_properties(const GameCore::GameObject& a_object,
                                            ECS::ParticleEffectComponent& a_effect,
                                            ECS::TransformComponent* a_transform,
                                            ECS::WorldTransformComponent* a_worldTransform)
    {
        std::string objectName{};
        if (!a_object.name(objectName) || objectName.empty())
        {
            objectName = k_defaultEffectName;
        }

        if (ImGui::CollapsingHeader("Basic Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Name: %s", objectName.c_str());
            ImGui::Text("EntityId: %llu", static_cast<unsigned long long>(a_object.entity_id()));
            ImGui::Checkbox("Loop", &a_effect.isLooping);
        }

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            draw_world_position_properties(a_transform, a_worldTransform);
        }
    }

    void EffectEditor::draw_world_position_properties(ECS::TransformComponent* a_transform,
                                                      ECS::WorldTransformComponent* a_worldTransform)
    {
        if (a_transform == nullptr)
        {
            ImGui::TextUnformatted("TransformComponent がありません。");
            return;
        }

        drag_float3("Location", a_transform->position, 0.01f);
        if (a_worldTransform != nullptr)
        {
            a_worldTransform->position = a_transform->position;
            read_float3("World Location", a_worldTransform->position);
        }
    }

    void EffectEditor::draw_emitter_properties(ECS::ParticleEffectComponent& a_effect, int32_t a_emitterNodeIndex)
    {
        ECS::EffectNode* emitter =
            a_emitterNodeIndex >= 0 && a_emitterNodeIndex < static_cast<int32_t>(a_effect.nodes.size())
                ? &a_effect.nodes[static_cast<size_t>(a_emitterNodeIndex)]
                : nullptr;
        if (emitter == nullptr)
        {
            return;
        }

        if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Playing", &a_effect.isPlaying);
            ImGui::Checkbox("Loop", &a_effect.isLooping);
            drag_random_seed(a_effect);
            drag_float3("Local Position", emitter->position, 0.01f);
        }

        if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ECS::EffectNode* generation =
                    ECS::find_effect_child_node(a_effect, a_emitterNodeIndex, ECS::EffectNodeKind::Generation);
                generation != nullptr)
            {
                draw_generation_properties(a_effect, *generation);
            }
        }

        if (ImGui::CollapsingHeader("Lifetime", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ECS::EffectNode* lifetime =
                    ECS::find_effect_child_node(a_effect, a_emitterNodeIndex, ECS::EffectNodeKind::Lifetime);
                lifetime != nullptr)
            {
                draw_lifetime_properties(a_effect, *lifetime);
            }
        }
    }

    void EffectEditor::draw_generation_properties(ECS::ParticleEffectComponent& a_effect, ECS::EffectNode& a_node)
    {
        ImGui::DragFloat("Spawn Rate", &a_node.spawnRate, 1.0f, 0.0f, 1000.0f);
        drag_node_capacity(a_node);
        ImGui::DragFloat("Accumulator", &a_node.spawnAccumulator, 0.01f, 0.0f, 1.0f);
        a_effect.spawnAccumulator = a_node.spawnAccumulator;
    }

    void EffectEditor::draw_lifetime_properties(ECS::ParticleEffectComponent& a_effect, ECS::EffectNode& a_node)
    {
        ImGui::DragFloat("Lifetime", &a_node.particleLifetime, 0.01f, 0.01f, 30.0f);
        ImGui::Text("Alive Count: %zu", a_effect.particles.size());

        const float capacity = a_effect.maxParticles == 0u
            ? 0.0f
            : static_cast<float>(a_effect.particles.size()) / static_cast<float>(a_effect.maxParticles);
        ImGui::ProgressBar(capacity, ImVec2(-1.0f, 0.0f));
    }

    void EffectEditor::draw_position_properties(ECS::EffectNode& a_node)
    {
        drag_float3("Local Offset", a_node.position, 0.01f);
    }

    void EffectEditor::draw_velocity_properties(ECS::EffectNode& a_node)
    {
        drag_float3("Initial Velocity", a_node.initialVelocity, 0.01f);
        drag_float3("Velocity Spread", a_node.velocitySpread, 0.01f);
        drag_float3("Acceleration", a_node.acceleration, 0.01f);
    }

    void EffectEditor::draw_color_properties(ECS::EffectNode& a_node)
    {
        color_edit4("Color Begin", a_node.startColor);
        color_edit4("Color End", a_node.endColor);
    }

    void EffectEditor::draw_renderer_properties(ECS::EffectNode& a_node)
    {
        ImGui::DragFloat("Size Begin", &a_node.startSize, 0.005f, 0.0f, 10.0f);
        ImGui::DragFloat("Size End", &a_node.endSize, 0.005f, 0.0f, 10.0f);

        a_node.isAdditive = true;
        bool isAdditive = a_node.isAdditive;
        ImGui::BeginDisabled();
        ImGui::Checkbox("Additive", &isAdditive);
        ImGui::EndDisabled();

        ImGui::TextUnformatted("Pipeline: EffectSprite");
        ImGui::TextUnformatted("Target: FinalColor");
    }

    void EffectEditor::draw_statistics_properties(const GameCore::GameObject& a_object,
                                                  const ECS::ParticleEffectComponent& a_effect)
    {
        ImGui::Text("EntityId: %llu", static_cast<unsigned long long>(a_object.entity_id()));
        ImGui::Text("Alive: %zu / %u", a_effect.particles.size(), a_effect.maxParticles);
        ImGui::Text("Accumulator: %.3f", a_effect.spawnAccumulator);
        ImGui::Text("Seed: %u", a_effect.randomSeed);

        const float capacity = a_effect.maxParticles == 0u
            ? 0.0f
            : static_cast<float>(a_effect.particles.size()) / static_cast<float>(a_effect.maxParticles);
        ImGui::ProgressBar(capacity, ImVec2(-1.0f, 0.0f));
    }
} // namespace Cue::Editor
