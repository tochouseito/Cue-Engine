#include "GameObject.h"

// === Engine includes ===
#include "GameWorld.h"

// === C++ includes ===
#include <utility>

namespace Cue::GameCore
{
    GameObject::GameObject() noexcept = default;

    GameObject::GameObject(GameWorld* a_world, EntityId a_entityId, Generation a_generation) noexcept
        : m_world(a_world), m_entityId(a_entityId), m_generation(a_generation)
    {
    }

    bool GameObject::is_valid() const noexcept
    {
        if (m_world == nullptr)
        {
            return false;
        }

        bool isAlive = false;
        const Result result = m_world->is_alive(m_entityId, m_generation, isAlive);
        return result && isAlive;
    }

    GameObject::operator bool() const noexcept
    {
        return is_valid();
    }

    EntityId GameObject::entity_id() const noexcept
    {
        return m_entityId;
    }

    Generation GameObject::generation() const noexcept
    {
        return m_generation;
    }

    Result GameObject::name(std::string& a_outName) const
    {
        if (!is_valid())
        {
            a_outName.clear();
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->get_object_name(m_entityId, a_outName);
    }

    Result GameObject::set_name(std::string_view a_name)
    {
        if (!is_valid())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_name(m_entityId, a_name);
    }

    Result GameObject::tag(std::string& a_outTag) const
    {
        if (!is_valid())
        {
            a_outTag.clear();
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->get_object_tag(m_entityId, a_outTag);
    }

    Result GameObject::set_tag(std::string_view a_tag)
    {
        if (!is_valid())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_tag(m_entityId, a_tag);
    }

    Result GameObject::is_active(bool& a_outIsActive) const
    {
        if (!is_valid())
        {
            a_outIsActive = false;
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->is_object_active(m_entityId, a_outIsActive);
    }

    Result GameObject::set_active(bool a_isActive)
    {
        if (!is_valid())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_active(m_entityId, a_isActive);
    }

    Result GameObject::is_persistent(bool& a_outIsPersistent) const
    {
        if (!is_valid())
        {
            a_outIsPersistent = false;
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->is_object_persistent(m_entityId, a_outIsPersistent);
    }

    Result GameObject::set_persistent(bool a_isPersistent)
    {
        if (!is_valid())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_persistent(m_entityId, a_isPersistent);
    }

    template <typename T> Result GameObject::get_component(T*& a_outComponent) noexcept
    {
        if (!is_valid())
        {
            a_outComponent = nullptr;
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->get_component<T>(m_entityId, a_outComponent);
    }

    template <typename T, typename... Args> Result GameObject::add_component(T*& a_outComponent, Args&&... a_args)
    {
        if (!is_valid())
        {
            a_outComponent = nullptr;
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->add_component<T>(m_entityId, a_outComponent, std::forward<Args>(a_args)...);
    }

    template <typename T> Result GameObject::has_component(bool& a_outHasComponent) const noexcept
    {
        if (!is_valid())
        {
            a_outHasComponent = false;
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->has_component<T>(m_entityId, a_outHasComponent);
    }

    template <typename T> Result GameObject::remove_component() noexcept
    {
        if (!is_valid())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->remove_component<T>(m_entityId);
    }

    Result GameObject::destroy() noexcept
    {
        if (!is_valid())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->destroy_object(m_entityId);
    }

    template Result GameObject::get_component<BaseComponent>(BaseComponent*&) noexcept;
    template Result GameObject::get_component<ECS::RenderableInfoComponent>(ECS::RenderableInfoComponent*&) noexcept;
    template Result GameObject::get_component<ECS::TransformComponent>(ECS::TransformComponent*&) noexcept;
    template Result GameObject::get_component<ECS::WorldTransformComponent>(ECS::WorldTransformComponent*&) noexcept;
    template Result GameObject::get_component<ECS::MeshFilterComponent>(ECS::MeshFilterComponent*&) noexcept;
    template Result GameObject::get_component<ECS::StaticMeshRendererComponent>(
        ECS::StaticMeshRendererComponent*&) noexcept;

    template Result GameObject::add_component<BaseComponent>(BaseComponent*&);
    template Result GameObject::add_component<ECS::RenderableInfoComponent>(ECS::RenderableInfoComponent*&);
    template Result GameObject::add_component<ECS::TransformComponent>(ECS::TransformComponent*&);
    template Result GameObject::add_component<ECS::WorldTransformComponent>(ECS::WorldTransformComponent*&);
    template Result GameObject::add_component<ECS::MeshFilterComponent>(ECS::MeshFilterComponent*&);
    template Result GameObject::add_component<ECS::StaticMeshRendererComponent>(ECS::StaticMeshRendererComponent*&);

    template Result GameObject::has_component<BaseComponent>(bool&) const noexcept;
    template Result GameObject::has_component<ECS::RenderableInfoComponent>(bool&) const noexcept;
    template Result GameObject::has_component<ECS::TransformComponent>(bool&) const noexcept;
    template Result GameObject::has_component<ECS::WorldTransformComponent>(bool&) const noexcept;
    template Result GameObject::has_component<ECS::MeshFilterComponent>(bool&) const noexcept;
    template Result GameObject::has_component<ECS::StaticMeshRendererComponent>(bool&) const noexcept;

    template Result GameObject::remove_component<BaseComponent>() noexcept;
    template Result GameObject::remove_component<ECS::RenderableInfoComponent>() noexcept;
    template Result GameObject::remove_component<ECS::TransformComponent>() noexcept;
    template Result GameObject::remove_component<ECS::WorldTransformComponent>() noexcept;
    template Result GameObject::remove_component<ECS::MeshFilterComponent>() noexcept;
    template Result GameObject::remove_component<ECS::StaticMeshRendererComponent>() noexcept;
} // namespace Cue::GameCore
