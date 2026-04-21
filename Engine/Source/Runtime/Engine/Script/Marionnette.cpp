#include "Marionnette.h"

// === Engine includes ===
#include "../GameCore/GameWorld.h"
#include "ScriptRuntime.h"

namespace Cue
{
    namespace
    {
        const MarionnetteClass g_marionnetteClass{
            "Marionnette",
            MarionnetteObject::static_class(),
            nullptr,
            nullptr,
            0,
            nullptr,
            0
        };

        const MarionnetteClass g_marionnetteComponentClass{
            "MarionnetteComponent",
            MarionnetteObject::static_class(),
            nullptr,
            nullptr,
            0,
            nullptr,
            0
        };
    }

    [[nodiscard]] const MarionnetteClass* Marionnette::static_class() noexcept
    {
        return &g_marionnetteClass;
    }

    [[nodiscard]] const MarionnetteClass* Marionnette::get_class() const noexcept
    {
        return static_class();
    }

    [[nodiscard]] bool Marionnette::is_valid() const noexcept
    {
        if (m_world == nullptr)
        {
            return false;
        }

        bool isAlive = false;
        const Result result = m_world->is_alive(m_entityId, m_generation, isAlive);
        return result && isAlive;
    }

    [[nodiscard]] GameCore::GameObject Marionnette::object() const noexcept
    {
        return GameCore::GameObject(m_world, m_entityId, m_generation);
    }

    [[nodiscard]] Result Marionnette::name(std::string& a_outName) const
    {
        return object().name(a_outName);
    }

    [[nodiscard]] Result Marionnette::set_name(std::string_view a_name)
    {
        return object().set_name(a_name);
    }

    [[nodiscard]] Result Marionnette::tag(std::string& a_outTag) const
    {
        return object().tag(a_outTag);
    }

    [[nodiscard]] Result Marionnette::set_tag(std::string_view a_tag)
    {
        return object().set_tag(a_tag);
    }

    [[nodiscard]] ECS::TransformComponent* Marionnette::get_transform() noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    [[nodiscard]] const ECS::TransformComponent* Marionnette::get_transform() const noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    [[nodiscard]] MarionnetteComponent* Marionnette::get_component_by_class(
        const MarionnetteClass* a_componentClass) noexcept
    {
        return m_runtime != nullptr
            ? m_runtime->find_marionnette_component_by_class(
                m_entityId, a_componentClass)
            : nullptr;
    }

    [[nodiscard]] const MarionnetteComponent* Marionnette::get_component_by_class(
        const MarionnetteClass* a_componentClass) const noexcept
    {
        return m_runtime != nullptr
            ? m_runtime->find_marionnette_component_by_class(
                m_entityId, a_componentClass)
            : nullptr;
    }

    void Marionnette::bind(ScriptRuntime* a_runtime, GameCore::GameWorld* a_world,
        GameCore::EntityId a_entityId,
        GameCore::Generation a_generation) noexcept
    {
        m_runtime = a_runtime;
        m_world = a_world;
        m_entityId = a_entityId;
        m_generation = a_generation;
    }

    void Marionnette::unbind() noexcept
    {
        m_runtime = nullptr;
        m_world = nullptr;
        m_entityId = GameCore::k_invalidEntityId;
        m_generation = 0;
    }

    [[nodiscard]] const MarionnetteClass* MarionnetteComponent::static_class() noexcept
    {
        return &g_marionnetteComponentClass;
    }

    [[nodiscard]] const MarionnetteClass* MarionnetteComponent::get_class() const noexcept
    {
        return static_class();
    }

    [[nodiscard]] bool MarionnetteComponent::is_valid() const noexcept
    {
        if (m_world == nullptr)
        {
            return false;
        }

        bool isAlive = false;
        const Result result = m_world->is_alive(m_entityId, m_generation, isAlive);
        return result && isAlive;
    }

    [[nodiscard]] GameCore::GameObject MarionnetteComponent::object() const noexcept
    {
        return GameCore::GameObject(m_world, m_entityId, m_generation);
    }

    [[nodiscard]] ECS::TransformComponent* MarionnetteComponent::get_transform() noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    [[nodiscard]] const ECS::TransformComponent* MarionnetteComponent::get_transform() const noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    [[nodiscard]] MarionnetteComponent* MarionnetteComponent::get_component_by_class(
        const MarionnetteClass* a_componentClass) noexcept
    {
        return m_owner != nullptr
            ? m_owner->get_component_by_class(a_componentClass)
            : nullptr;
    }

    [[nodiscard]] const MarionnetteComponent* MarionnetteComponent::get_component_by_class(
        const MarionnetteClass* a_componentClass) const noexcept
    {
        return m_owner != nullptr
            ? m_owner->get_component_by_class(a_componentClass)
            : nullptr;
    }

    void MarionnetteComponent::bind(ScriptRuntime* a_runtime, GameCore::GameWorld* a_world,
        GameCore::EntityId a_entityId,
        GameCore::Generation a_generation,
        Marionnette* a_owner) noexcept
    {
        m_runtime = a_runtime;
        m_world = a_world;
        m_entityId = a_entityId;
        m_generation = a_generation;
        m_owner = a_owner;
    }

    void MarionnetteComponent::unbind() noexcept
    {
        m_runtime = nullptr;
        m_world = nullptr;
        m_entityId = GameCore::k_invalidEntityId;
        m_generation = 0;
        m_owner = nullptr;
        m_isEnabled = true;
    }
}
