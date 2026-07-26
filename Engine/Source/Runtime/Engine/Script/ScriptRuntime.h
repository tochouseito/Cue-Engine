#pragma once

/// **********************************************************************
/// Runtime World の ScriptComponent と Script module instance を同期する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <Native/ScriptAbi.h>

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameCoreTypes.h"
#include "ScriptModuleApi.h"

// === C++ includes ===
#include <string>
#include <unordered_map>

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue::Script
{
    class ScriptModule;

    /// @brief Runtime World の ScriptComponent と DLL 内 instance を対応付け
    ///
    /// Editor の authoring World は変更せず、Play 用に複製した Runtime World だけを Script の対象とする
    class ScriptRuntime final
    {
    public:
        explicit ScriptRuntime(GameCore::GameWorld& a_world) noexcept;
        ~ScriptRuntime();

        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        ScriptRuntime(ScriptRuntime&&) = delete;
        ScriptRuntime& operator=(ScriptRuntime&&) = delete;

        /// @brief instance を生成・破棄する module を設定
        ///
        /// module は非所有であり、既存 instance の reset 後にだけ差し替え可能
        void set_module(const ScriptModule* a_module) noexcept;

        /// @brief Runtime World の有効な ScriptComponent から instance を生成
        [[nodiscard]] Result start() noexcept;

        /// @brief Component の変更を同期してから全 instance の OnUpdate を呼び出し
        ///
        /// Object の削除、無効化、className 変更による古い instance の残留を防止
        [[nodiscard]] Result update(float a_deltaTimeSeconds) noexcept;

        /// @brief 保持する全 instance を DLL 側で破棄
        ///
        /// DLL unload より前に完了させ、DLL が所有する object を解放できる状態を維持
        [[nodiscard]] Result reset() noexcept;

        /// @brief GameScript DLL が Runtime World を操作するための ABI を返す
        ///
        /// API は Runtime World だけを参照し、authoring World を DLL から変更させない
        [[nodiscard]] const Core::Native::ScriptEngineApi& script_engine_api() const noexcept;

    private:
        struct Binding final
        {
            // Component の className と比較して、class 差し替え時に旧 instance を破棄する
            std::string className{};

            // DLL 所有の object を Engine から参照するための不透明な handle
            ScriptInstanceHandle instanceHandle = k_invalidScriptInstanceHandle;

            // Entity ID 再利用時に旧世代の Script instance を残さないため、bind 時点の世代を保持する
            GameCore::Generation generation = 0u;

        };

        [[nodiscard]] Result sync_instances() noexcept;
        [[nodiscard]] Result create_instance(
            GameCore::EntityId a_entityId,
            GameCore::Generation a_generation,
            const std::string& a_className) noexcept;
        [[nodiscard]] Result destroy_instance(GameCore::EntityId a_entityId) noexcept;

        [[nodiscard]] static Core::Native::ScriptAbiResult script_is_entity_valid(
            void* a_userData,
            Core::Native::ScriptEntityHandle a_entity,
            uint8_t* a_outIsValid) noexcept;
        [[nodiscard]] static Core::Native::ScriptAbiResult script_read_transform(
            void* a_userData,
            Core::Native::ScriptEntityHandle a_entity,
            Core::Native::ScriptTransform* a_outTransform) noexcept;
        [[nodiscard]] static Core::Native::ScriptAbiResult script_write_transform(
            void* a_userData,
            Core::Native::ScriptEntityHandle a_entity,
            const Core::Native::ScriptTransform* a_transform) noexcept;
        [[nodiscard]] static Core::Native::ScriptAbiResult to_script_abi_result(
            const Result& a_result) noexcept;

        [[nodiscard]] Result get_transform_component(
            Core::Native::ScriptEntityHandle a_entity,
            ECS::TransformComponent*& a_outTransform) noexcept;

        GameCore::GameWorld& m_world; // Play 中だけ使用する Runtime World への非所有参照
        std::unordered_map<GameCore::EntityId, Binding> m_bindings{}; // Runtime Entity ごとの DLL instance
        Core::Native::ScriptEngineApi m_scriptEngineApi{}; // GameScript DLL へ Runtime World 操作だけを公開する callback table
        const ScriptModule* m_module = nullptr; // bindings の全 instance より長く生存する非所有 module
    };
} // namespace Cue::Script
