#pragma once

/// **********************************************************************
/// Script Asset がアタッチ先 GameObject と Component へ到達する基底を定義する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameCoreTypes.h"
#include "GameCore/GameObject.h"
#include "GameCore/GameWorld.h"

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue::Script
{
    class ScriptRuntime;

    /// @brief Scene 上の 1 Entity に対応する Script 側の owner
    ///
    /// Script は authoring World ではなく、Play 用 Runtime World の GameObject をこの基底経由で操作する
    class Marionnette
    {
    public:
        Marionnette() = default;
        virtual ~Marionnette() = default;

        Marionnette(const Marionnette&) = delete;
        Marionnette& operator=(const Marionnette&) = delete;
        Marionnette(Marionnette&&) = delete;
        Marionnette& operator=(Marionnette&&) = delete;

        /// @brief アタッチ先 Entity が同じ世代で Runtime World に存在するかを返却
        [[nodiscard]] bool is_valid() const noexcept;

        /// @brief アタッチ先 Entity の ID を返却
        [[nodiscard]] GameCore::EntityId entity_id() const noexcept;

        /// @brief アタッチ時点の Entity 世代番号を返却
        [[nodiscard]] GameCore::Generation generation() const noexcept;

        /// @brief アタッチ先 Entity を表す GameObject handle を返却
        [[nodiscard]] GameCore::GameObject object() const noexcept;

        /// @brief アタッチ先 Object の表示名を取得
        [[nodiscard]] Result name(std::string& a_outName) const;

        /// @brief アタッチ先 Object の表示名を変更
        [[nodiscard]] Result set_name(std::string_view a_name);

        /// @brief アタッチ先 Object の tag を取得
        [[nodiscard]] Result tag(std::string& a_outTag) const;

        /// @brief アタッチ先 Object の tag を変更
        [[nodiscard]] Result set_tag(std::string_view a_tag);

        /// @brief アタッチ先 Object の指定 Component を取得
        template <typename T>
        [[nodiscard]] T* get_component() noexcept;

        /// @brief アタッチ先 Object の指定 Component を読み取り専用で取得
        template <typename T>
        [[nodiscard]] const T* get_component() const noexcept;

        /// @brief アタッチ先 Object のローカル Transform を取得
        [[nodiscard]] ECS::TransformComponent* get_transform() noexcept;

        /// @brief アタッチ先 Object のローカル Transform を読み取り専用で取得
        [[nodiscard]] const ECS::TransformComponent* get_transform() const noexcept;

    private:
        friend class ScriptRuntime;

        // Script instance の生存中だけ Runtime World の Entity と対応付け、削除済み Entity の再利用を世代で検出する
        void bind(
            ScriptRuntime* a_runtime,
            GameCore::GameWorld* a_world,
            GameCore::EntityId a_entityId,
            GameCore::Generation a_generation) noexcept;
        void unbind() noexcept;

        ScriptRuntime* m_runtime = nullptr;
        GameCore::GameWorld* m_world = nullptr;
        GameCore::EntityId m_entityId = GameCore::k_invalidEntityId;
        GameCore::Generation m_generation = 0u;
    };

    /// @brief Marionnette に付随する Script Asset の lifecycle 基底
    ///
    /// 同じ Entity に結び付くため、Script Asset から owner と Transform へ一貫して到達できる
    class MarionnetteComponent
    {
    public:
        MarionnetteComponent() = default;
        virtual ~MarionnetteComponent() = default;

        MarionnetteComponent(const MarionnetteComponent&) = delete;
        MarionnetteComponent& operator=(const MarionnetteComponent&) = delete;
        MarionnetteComponent(MarionnetteComponent&&) = delete;
        MarionnetteComponent& operator=(MarionnetteComponent&&) = delete;

        /// @brief アタッチ先 Entity が同じ世代で Runtime World に存在するかを返却
        [[nodiscard]] bool is_valid() const noexcept;

        /// @brief Script Asset が更新対象かを返却
        [[nodiscard]] bool is_enabled() const noexcept;

        /// @brief Script Asset の更新対象状態を設定
        void set_enabled(bool a_isEnabled) noexcept;

        /// @brief アタッチ先 Entity の ID を返却
        [[nodiscard]] GameCore::EntityId entity_id() const noexcept;

        /// @brief アタッチ時点の Entity 世代番号を返却
        [[nodiscard]] GameCore::Generation generation() const noexcept;

        /// @brief 所属する Marionnette を返却
        [[nodiscard]] Marionnette* owner() noexcept;

        /// @brief 所属する Marionnette を読み取り専用で返却
        [[nodiscard]] const Marionnette* owner() const noexcept;

        /// @brief アタッチ先 Entity を表す GameObject handle を返却
        [[nodiscard]] GameCore::GameObject object() const noexcept;

        /// @brief アタッチ先 Object の指定 Component を取得
        template <typename T>
        [[nodiscard]] T* get_component() noexcept;

        /// @brief アタッチ先 Object の指定 Component を読み取り専用で取得
        template <typename T>
        [[nodiscard]] const T* get_component() const noexcept;

        /// @brief アタッチ先 Object のローカル Transform を取得
        [[nodiscard]] ECS::TransformComponent* get_transform() noexcept;

        /// @brief アタッチ先 Object のローカル Transform を読み取り専用で取得
        [[nodiscard]] const ECS::TransformComponent* get_transform() const noexcept;

        /// @brief instance の Entity への接続後に一度呼び出す lifecycle
        virtual void awake() noexcept;

        /// @brief 最初の更新前に一度呼び出す lifecycle
        virtual void start() noexcept;

        /// @brief ScriptSystem の更新周期で呼び出す lifecycle
        virtual void update(float a_deltaTimeSeconds) noexcept;

        /// @brief Entity との接続を外す前に一度呼び出す lifecycle
        virtual void on_destroy() noexcept;

    private:
        friend class ScriptRuntime;

        // owner と同じ Entity へ束ね、Script Asset が別 Object の Component を誤って操作しないようにする
        void bind(
            ScriptRuntime* a_runtime,
            GameCore::GameWorld* a_world,
            GameCore::EntityId a_entityId,
            GameCore::Generation a_generation,
            Marionnette* a_owner) noexcept;
        void unbind() noexcept;

        ScriptRuntime* m_runtime = nullptr;
        GameCore::GameWorld* m_world = nullptr;
        Marionnette* m_owner = nullptr;
        GameCore::EntityId m_entityId = GameCore::k_invalidEntityId;
        GameCore::Generation m_generation = 0u;
        bool m_isEnabled = true;
    };

    template <typename T>
    T* Marionnette::get_component() noexcept
    {
        if (!is_valid())
        {
            return nullptr;
        }

        T* component = nullptr;
        const Result result = object().get_component(component);
        return result ? component : nullptr;
    }

    template <typename T>
    const T* Marionnette::get_component() const noexcept
    {
        if (!is_valid())
        {
            return nullptr;
        }

        T* component = nullptr;
        const Result result = object().get_component(component);
        return result ? component : nullptr;
    }

    template <typename T>
    T* MarionnetteComponent::get_component() noexcept
    {
        return m_owner != nullptr ? m_owner->get_component<T>() : nullptr;
    }

    template <typename T>
    const T* MarionnetteComponent::get_component() const noexcept
    {
        return m_owner != nullptr ? m_owner->get_component<T>() : nullptr;
    }
} // namespace Cue::Script
