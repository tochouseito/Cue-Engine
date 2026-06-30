#include "GameObjectProto.h"

// === Engine includes ===
#include "Components.h"

// === C++ includes ===
#include <utility>

namespace Cue::GameCore
{
    GameObjectProto::GameObjectProto() : m_name(""), m_tag("Default")
    {
    }

    GameObjectProto::GameObjectProto(std::string name, std::string tag) : m_name(std::move(name)), m_tag(std::move(tag))
    {
    }

    GameObjectProto::~GameObjectProto() = default;

    const std::string& GameObjectProto::name() const
    {
        return m_name;
    }

    const std::string& GameObjectProto::tag() const
    {
        return m_tag;
    }

    template <ECS::ComponentType T> void GameObjectProto::add_component(const T& a_comp)
    {
        register_component_type<T>();

        Prototype::add_component<T>(a_comp);
    }

    template <ECS::ComponentType T> void GameObjectProto::set_component(const T& a_comp)
    {
        register_component_type<T>();

        Prototype::set_component<T>(a_comp);
    }

    void GameObjectProto::restore_components_into(ECS::Entity a_entity, ECS::ECSManager& a_ecs) const
    {
        instantiate_components(a_entity, a_ecs);
    }

    GameObjectProto GameObjectProto::from_entity(ECS::ECSManager& a_ecs, ECS::Entity a_e, const std::string& a_name,
                                                 const std::string& a_tag)
    {
        GameObjectProto proto(a_name, a_tag);

        // ECS::Prototype の型消去 storage へ component 群をコピーする
        proto.populate_from_entity(a_ecs, a_e);
        return proto;
    }

    template <ECS::ComponentType T> void GameObjectProto::register_component_type()
    {
        // ECS::Prototype の型消去 storage を Entity へ展開するための型別関数。
        Prototype::register_copy_func<T>();

        Prototype::register_prefab_restore<T>();
    }

    ECS::Entity GameObjectProto::create_entity(ECS::ECSManager& a_ecs) const
    {
        return Prototype::create_entity(a_ecs);
    }

    template void GameObjectProto::add_component<BaseComponent>(const BaseComponent&);
    template void GameObjectProto::add_component<ECS::RenderableInfoComponent>(const ECS::RenderableInfoComponent&);
    template void GameObjectProto::add_component<ECS::TransformComponent>(const ECS::TransformComponent&);
    template void GameObjectProto::add_component<ECS::WorldTransformComponent>(const ECS::WorldTransformComponent&);
    template void GameObjectProto::add_component<ECS::CameraComponent>(const ECS::CameraComponent&);
    template void GameObjectProto::add_component<ECS::MeshFilterComponent>(const ECS::MeshFilterComponent&);
    template void GameObjectProto::add_component<ECS::StaticMeshRendererComponent>(
        const ECS::StaticMeshRendererComponent&);

    template void GameObjectProto::set_component<BaseComponent>(const BaseComponent&);
    template void GameObjectProto::set_component<ECS::RenderableInfoComponent>(const ECS::RenderableInfoComponent&);
    template void GameObjectProto::set_component<ECS::TransformComponent>(const ECS::TransformComponent&);
    template void GameObjectProto::set_component<ECS::WorldTransformComponent>(const ECS::WorldTransformComponent&);
    template void GameObjectProto::set_component<ECS::CameraComponent>(const ECS::CameraComponent&);
    template void GameObjectProto::set_component<ECS::MeshFilterComponent>(const ECS::MeshFilterComponent&);
    template void GameObjectProto::set_component<ECS::StaticMeshRendererComponent>(
        const ECS::StaticMeshRendererComponent&);

    template void GameObjectProto::register_component_type<BaseComponent>();
    template void GameObjectProto::register_component_type<ECS::RenderableInfoComponent>();
    template void GameObjectProto::register_component_type<ECS::TransformComponent>();
    template void GameObjectProto::register_component_type<ECS::WorldTransformComponent>();
    template void GameObjectProto::register_component_type<ECS::CameraComponent>();
    template void GameObjectProto::register_component_type<ECS::MeshFilterComponent>();
    template void GameObjectProto::register_component_type<ECS::StaticMeshRendererComponent>();
} // namespace Cue::GameCore
