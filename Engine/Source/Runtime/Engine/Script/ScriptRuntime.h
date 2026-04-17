#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Native/ScriptAbi.h>

// === Engine includes ===
#include "../GameCore/Components.h"
#include "../GameCore/GameCoreTypes.h"

// === C++ includes ===
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue
{
    class ScriptModule;

    class ScriptRuntime final
    {
    public:
        struct StateSnapshot final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            std::string className{};
            std::vector<std::byte> bytes{};
        };

        explicit ScriptRuntime(GameCore::GameWorld& a_gameWorld) noexcept;
        ~ScriptRuntime();

        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        ScriptRuntime(ScriptRuntime&&) = delete;
        ScriptRuntime& operator=(ScriptRuntime&&) = delete;

        [[nodiscard]] const CueEngineApi& engine_api() const noexcept;
        [[nodiscard]] const std::vector<std::string>&
            registered_script_classes() const noexcept;
        [[nodiscard]] bool has_registered_script_class(
            std::string_view a_className) const noexcept;
        [[nodiscard]] const std::vector<ECS::ScriptFieldValue>&
            script_field_defaults(std::string_view a_className) const noexcept;
        void activate() noexcept;
        void set_module(const ScriptModule* a_module) noexcept;
        [[nodiscard]] Result set_game_world(GameCore::GameWorld& a_gameWorld) noexcept;
        [[nodiscard]] Result capture_instance_states(
            std::vector<StateSnapshot>& a_outSnapshots) const noexcept;
        [[nodiscard]] Result restore_instance_states(
            const std::vector<StateSnapshot>& a_snapshots) noexcept;

        [[nodiscard]] Result update(float a_deltaTimeSeconds) noexcept;
        [[nodiscard]] Result reset() noexcept;

    private:
        struct ScriptBinding final
        {
            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            std::string className{};
            std::vector<ECS::ScriptFieldValue> fieldValues{};
        };

        struct ScriptClassInfo final
        {
            std::string className{};
            std::vector<ECS::ScriptFieldValue> fieldDefaults{};
        };

        [[nodiscard]] Result sync_instances() noexcept;
        [[nodiscard]] Result create_instance(
            GameCore::EntityId a_entityId,
            const ECS::ScriptComponent& a_scriptComponent) noexcept;
        [[nodiscard]] Result destroy_instance(GameCore::EntityId a_entityId) noexcept;
        [[nodiscard]] Result destroy_all_instances() noexcept;
        [[nodiscard]] std::vector<ECS::ScriptFieldValue> resolve_script_field_values(
            const ECS::ScriptComponent& a_scriptComponent) const;

        [[nodiscard]] static CueResult CUE_SCRIPT_CALL log_bridge(
            CueLogSeverity a_severity,
            CueStringView a_message);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL register_script_class_bridge(
            CueStringView a_scriptClassName);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL register_script_field_bridge(
            CueStringView a_scriptClassName,
            const CueScriptFieldValue* a_fieldValue);
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL is_entity_valid_bridge(
            CueEntityHandle a_entityHandle);
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL has_transform_bridge(
            CueEntityHandle a_entityHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL get_transform_bridge(
            CueEntityHandle a_entityHandle,
            CueTransformData* a_outTransform);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL set_transform_bridge(
            CueEntityHandle a_entityHandle,
            const CueTransformData* a_transform);

        [[nodiscard]] CueResult log_internal(
            CueLogSeverity a_severity,
            CueStringView a_message) noexcept;
        [[nodiscard]] CueResult register_script_class_internal(
            CueStringView a_scriptClassName) noexcept;
        [[nodiscard]] CueResult register_script_field_internal(
            CueStringView a_scriptClassName,
            const CueScriptFieldValue* a_fieldValue) noexcept;
        [[nodiscard]] uint8_t is_entity_valid_internal(
            CueEntityHandle a_entityHandle) const noexcept;
        [[nodiscard]] uint8_t has_transform_internal(
            CueEntityHandle a_entityHandle) const noexcept;
        [[nodiscard]] CueResult get_transform_internal(
            CueEntityHandle a_entityHandle,
            CueTransformData* a_outTransform) const noexcept;
        [[nodiscard]] CueResult set_transform_internal(
            CueEntityHandle a_entityHandle,
            const CueTransformData* a_transform) noexcept;

        [[nodiscard]] static Result convert_script_result(CueResult a_result) noexcept;
        [[nodiscard]] static CueResult convert_result_code(const Result& a_result) noexcept;
        [[nodiscard]] static GameCore::EntityId to_entity_id(
            CueEntityHandle a_entityHandle) noexcept;
        [[nodiscard]] static CueEntityHandle to_entity_handle(
            GameCore::EntityId a_entityId) noexcept;

        static ScriptRuntime* s_activeInstance;

        GameCore::GameWorld* m_gameWorld = nullptr;
        const ScriptModule* m_module = nullptr;
        CueEngineApi m_engineApi{};
        std::unordered_map<GameCore::EntityId, ScriptBinding> m_bindings{};
        std::vector<std::string> m_registeredScriptClasses{};
        std::unordered_map<std::string, ScriptClassInfo> m_scriptClassInfos{};
    };
}
