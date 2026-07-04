#pragma once

/// **********************************************************************
/// ParticleEffectComponent を Effekseer 風に編集する Editor パネル
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GameCore
{
    class GameObject;
    class GameWorld;
}

namespace Cue::ECS
{
    enum class EffectNodeKind : uint8_t;
    struct EffectNode;
    struct ParticleEffectComponent;
    struct TransformComponent;
    struct WorldTransformComponent;
}

namespace Cue::Editor
{
    /// @brief Runtime の ParticleEffectComponent を直接編集する Editor View。
    class EffectEditor final
    {
    public:
        /// @brief 参照先 World と選択 Entity を保持して初期化する。
        EffectEditor(GameCore::GameWorld* a_gameWorld,
                     GameCore::EntityId* a_selectedEntityId) noexcept;

        EffectEditor(const EffectEditor&) = delete;
        EffectEditor& operator=(const EffectEditor&) = delete;
        EffectEditor(EffectEditor&&) = delete;
        EffectEditor& operator=(EffectEditor&&) = delete;
        ~EffectEditor();

        /// @brief 参照先 World を更新する。
        void set_game_world(GameCore::GameWorld* a_gameWorld) noexcept;

        /// @brief Effect 編集 UI を描画する。
        void update();

    private:
        [[nodiscard]] bool find_effect_object(GameCore::GameObject& a_outObject,
                                              ECS::ParticleEffectComponent*& a_outEffect,
                                              ECS::TransformComponent*& a_outTransform,
                                              ECS::WorldTransformComponent*& a_outWorldTransform);
        [[nodiscard]] bool find_selected_object(GameCore::GameObject& a_outObject);
        [[nodiscard]] bool try_get_effect(GameCore::GameObject& a_object,
                                          ECS::ParticleEffectComponent*& a_outEffect,
                                          ECS::TransformComponent*& a_outTransform,
                                          ECS::WorldTransformComponent*& a_outWorldTransform);

        void draw_toolbar(ECS::ParticleEffectComponent& a_effect);
        [[nodiscard]] const char* node_kind_name(ECS::EffectNodeKind a_kind) const noexcept;
        void draw_node_tree(ECS::ParticleEffectComponent& a_effect);
        void draw_node_item(ECS::ParticleEffectComponent& a_effect, int32_t a_nodeIndex);
        void draw_node_properties(const GameCore::GameObject& a_object,
                                  ECS::ParticleEffectComponent& a_effect,
                                  ECS::TransformComponent* a_transform,
                                  ECS::WorldTransformComponent* a_worldTransform);
        void draw_root_properties(const GameCore::GameObject& a_object,
                                  ECS::ParticleEffectComponent& a_effect,
                                  ECS::TransformComponent* a_transform,
                                  ECS::WorldTransformComponent* a_worldTransform);
        void draw_world_position_properties(ECS::TransformComponent* a_transform,
                                            ECS::WorldTransformComponent* a_worldTransform);
        void draw_emitter_properties(ECS::ParticleEffectComponent& a_effect, int32_t a_emitterNodeIndex);
        void draw_generation_properties(ECS::ParticleEffectComponent& a_effect, ECS::EffectNode& a_node);
        void draw_lifetime_properties(ECS::ParticleEffectComponent& a_effect, ECS::EffectNode& a_node);
        void draw_position_properties(ECS::EffectNode& a_node);
        void draw_velocity_properties(ECS::EffectNode& a_node);
        void draw_color_properties(ECS::EffectNode& a_node);
        void draw_renderer_properties(ECS::EffectNode& a_node);
        void draw_statistics_properties(const GameCore::GameObject& a_object,
                                        const ECS::ParticleEffectComponent& a_effect);

        GameCore::GameWorld* m_gameWorld = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        int32_t m_selectedNodeIndex = 1;
    };
} // namespace Cue::Editor
