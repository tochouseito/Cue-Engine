#pragma once

// === Base includes ===
#include <Result.h>

// === Engine includes ===
#include "../GameCore/Components.h"
#include "../GameCore/GameCoreTypes.h"
#include "../GameCore/GameWorld.h"
#include "MarionnetteObject.h"

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue
{
    class ScriptRuntime;
    class MarionnetteComponent;

    /// @brief Scene 上の 1 Entity に対応する script runtime 側の owner です。
    /// Script から見た Marionnette は UE の Actor に近い責務を持ちます。
    /// 1 つの owner が 1 つの Entity を表し、その Entity 上の
    /// MarionnetteComponent 群を束ねます。
    class Marionnette : public MarionnetteObject
    {
    public:
        [[nodiscard]] static const MarionnetteClass* static_class() noexcept;
        [[nodiscard]] const MarionnetteClass* get_class() const noexcept override;

        Marionnette() = default;
        virtual ~Marionnette() = default;

        [[nodiscard]] bool is_valid() const noexcept;
        [[nodiscard]] GameCore::EntityId entity_id() const noexcept { return m_entityId; }
        [[nodiscard]] GameCore::Generation generation() const noexcept
        {
            return m_generation;
        }
        [[nodiscard]] ScriptRuntime* runtime() noexcept { return m_runtime; }
        [[nodiscard]] const ScriptRuntime* runtime() const noexcept
        {
            return m_runtime;
        }
        [[nodiscard]] GameCore::GameWorld* world() noexcept { return m_world; }
        [[nodiscard]] const GameCore::GameWorld* world() const noexcept
        {
            return m_world;
        }
        [[nodiscard]] GameCore::GameObject object() const noexcept;

        [[nodiscard]] Result name(std::string& a_outName) const;
        [[nodiscard]] Result set_name(std::string_view a_name);
        [[nodiscard]] Result tag(std::string& a_outTag) const;
        [[nodiscard]] Result set_tag(std::string_view a_tag);

        template <typename T>
        [[nodiscard]] T* get_component() noexcept;

        template <typename T>
        [[nodiscard]] const T* get_component() const noexcept;

        [[nodiscard]] MarionnetteComponent* get_component_by_class(
            const MarionnetteClass* a_componentClass) noexcept;
        [[nodiscard]] const MarionnetteComponent* get_component_by_class(
            const MarionnetteClass* a_componentClass) const noexcept;

        [[nodiscard]] ECS::TransformComponent* get_transform() noexcept;
        [[nodiscard]] const ECS::TransformComponent* get_transform() const noexcept;

        void bind(ScriptRuntime* a_runtime, GameCore::GameWorld* a_world,
            GameCore::EntityId a_entityId,
            GameCore::Generation a_generation) noexcept;
        void unbind() noexcept;

    protected:
        ScriptRuntime* m_runtime = nullptr;
        GameCore::GameWorld* m_world = nullptr;
        GameCore::EntityId m_entityId = GameCore::k_invalidEntityId;
        GameCore::Generation m_generation = 0;
    };

    /// @brief Marionnette に所属する script runtime 側の component です。
    /// MarionnetteComponent は単独では存在せず、常に owner Marionnette と
    /// 同じ Entity に結び付きます。
    class MarionnetteComponent : public MarionnetteObject
    {
    public:
        [[nodiscard]] static const MarionnetteClass* static_class() noexcept;
        [[nodiscard]] const MarionnetteClass* get_class() const noexcept override;

        MarionnetteComponent() = default;
        virtual ~MarionnetteComponent() = default;

        [[nodiscard]] bool is_valid() const noexcept;
        [[nodiscard]] bool is_enabled() const noexcept { return m_isEnabled; }
        void set_enabled(bool a_isEnabled) noexcept { m_isEnabled = a_isEnabled; }

        [[nodiscard]] GameCore::EntityId entity_id() const noexcept { return m_entityId; }
        [[nodiscard]] GameCore::Generation generation() const noexcept
        {
            return m_generation;
        }
        [[nodiscard]] ScriptRuntime* runtime() noexcept { return m_runtime; }
        [[nodiscard]] const ScriptRuntime* runtime() const noexcept
        {
            return m_runtime;
        }
        [[nodiscard]] GameCore::GameWorld* world() noexcept { return m_world; }
        [[nodiscard]] const GameCore::GameWorld* world() const noexcept
        {
            return m_world;
        }
        [[nodiscard]] Marionnette* owner() noexcept { return m_owner; }
        [[nodiscard]] const Marionnette* owner() const noexcept { return m_owner; }
        [[nodiscard]] GameCore::GameObject object() const noexcept;

        template <typename T>
        [[nodiscard]] T* get_component() noexcept;

        template <typename T>
        [[nodiscard]] const T* get_component() const noexcept;

        [[nodiscard]] MarionnetteComponent* get_component_by_class(
            const MarionnetteClass* a_componentClass) noexcept;
        [[nodiscard]] const MarionnetteComponent* get_component_by_class(
            const MarionnetteClass* a_componentClass) const noexcept;

        [[nodiscard]] ECS::TransformComponent* get_transform() noexcept;
        [[nodiscard]] const ECS::TransformComponent* get_transform() const noexcept;

        virtual void awake() {}
        virtual void start() {}
        virtual void update(float a_deltaTimeSeconds)
        {
            (void)a_deltaTimeSeconds;
        }
        virtual void on_destroy() {}

        void bind(ScriptRuntime* a_runtime, GameCore::GameWorld* a_world,
            GameCore::EntityId a_entityId,
            GameCore::Generation a_generation, Marionnette* a_owner) noexcept;
        void unbind() noexcept;

    protected:
        ScriptRuntime* m_runtime = nullptr;
        GameCore::GameWorld* m_world = nullptr;
        GameCore::EntityId m_entityId = GameCore::k_invalidEntityId;
        GameCore::Generation m_generation = 0;
        Marionnette* m_owner = nullptr;
        bool m_isEnabled = true;
    };

    template <typename T>
    [[nodiscard]] T* Marionnette::get_component() noexcept
    {
        if (!is_valid())
        {
            return nullptr;
        }

        GameCore::GameObject gameObject = object();
        T* component = nullptr;
        const Result result = gameObject.get_component(component);
        return result ? component : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* Marionnette::get_component() const noexcept
    {
        if (!is_valid())
        {
            return nullptr;
        }

        GameCore::GameObject gameObject = object();
        T* component = nullptr;
        const Result result = gameObject.get_component(component);
        return result ? component : nullptr;
    }

    template <typename T>
    [[nodiscard]] T* MarionnetteComponent::get_component() noexcept
    {
        if (!is_valid())
        {
            return nullptr;
        }

        GameCore::GameObject gameObject = object();
        T* component = nullptr;
        const Result result = gameObject.get_component(component);
        return result ? component : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* MarionnetteComponent::get_component() const noexcept
    {
        if (!is_valid())
        {
            return nullptr;
        }

        GameCore::GameObject gameObject = object();
        T* component = nullptr;
        const Result result = gameObject.get_component(component);
        return result ? component : nullptr;
    }
}
