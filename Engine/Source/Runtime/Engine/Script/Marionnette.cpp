#include "Marionnette.h"

namespace Cue::Script
{
    bool Marionnette::is_valid() const noexcept
    {
        if (m_world == nullptr)
        {
            return false;
        }

        bool isAlive = false;
        const Result result = m_world->is_alive(m_entityId, m_generation, isAlive);
        return result && isAlive;
    }

    GameCore::EntityId Marionnette::entity_id() const noexcept
    {
        return m_entityId;
    }

    GameCore::Generation Marionnette::generation() const noexcept
    {
        return m_generation;
    }

    GameCore::GameObject Marionnette::object() const noexcept
    {
        return GameCore::GameObject(m_world, m_entityId, m_generation);
    }

    Result Marionnette::name(std::string& a_outName) const
    {
        return object().name(a_outName);
    }

    Result Marionnette::set_name(std::string_view a_name)
    {
        return object().set_name(a_name);
    }

    Result Marionnette::tag(std::string& a_outTag) const
    {
        return object().tag(a_outTag);
    }

    Result Marionnette::set_tag(std::string_view a_tag)
    {
        return object().set_tag(a_tag);
    }

    ECS::TransformComponent* Marionnette::get_transform() noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    const ECS::TransformComponent* Marionnette::get_transform() const noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    void Marionnette::bind(
        ScriptRuntime* a_runtime,
        GameCore::GameWorld* a_world,
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
        m_generation = 0u;
    }

    bool MarionnetteComponent::is_valid() const noexcept
    {
        return m_owner != nullptr && m_owner->is_valid();
    }

    bool MarionnetteComponent::is_enabled() const noexcept
    {
        return m_isEnabled;
    }

    void MarionnetteComponent::set_enabled(bool a_isEnabled) noexcept
    {
        m_isEnabled = a_isEnabled;
    }

    GameCore::EntityId MarionnetteComponent::entity_id() const noexcept
    {
        return m_entityId;
    }

    GameCore::Generation MarionnetteComponent::generation() const noexcept
    {
        return m_generation;
    }

    Marionnette* MarionnetteComponent::owner() noexcept
    {
        return m_owner;
    }

    const Marionnette* MarionnetteComponent::owner() const noexcept
    {
        return m_owner;
    }

    GameCore::GameObject MarionnetteComponent::object() const noexcept
    {
        return m_owner != nullptr ? m_owner->object() : GameCore::GameObject{};
    }

    ECS::TransformComponent* MarionnetteComponent::get_transform() noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    const ECS::TransformComponent* MarionnetteComponent::get_transform() const noexcept
    {
        return get_component<ECS::TransformComponent>();
    }

    void MarionnetteComponent::awake() noexcept
    {
    }

    void MarionnetteComponent::start() noexcept
    {
    }

    void MarionnetteComponent::update(float a_deltaTimeSeconds) noexcept
    {
        (void)a_deltaTimeSeconds;
    }

    void MarionnetteComponent::on_destroy() noexcept
    {
    }

    void MarionnetteComponent::bind(
        ScriptRuntime* a_runtime,
        GameCore::GameWorld* a_world,
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
        m_generation = 0u;
        m_owner = nullptr;
        m_isEnabled = true;
    }
} // namespace Cue::Script
