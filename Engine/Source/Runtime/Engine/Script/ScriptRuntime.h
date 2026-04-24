#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Native/ScriptAbi.h>

// === Engine includes ===
#include "../GameCore/Components.h"
#include "../GameCore/GameCoreTypes.h"
#include "Marionnette.h"

// === C++ includes ===
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue::PAL
{
    class IPlatform;
}

namespace Cue
{
    class ScriptModule;

    class ScriptRuntime final
    {
    public:
        struct StateRestoreIssue final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            std::string className{};
            std::string detail{};
        };

        struct StateRestoreReport final
        {
            uint32_t restoredCount = 0;
            uint32_t skippedCount = 0;
            std::vector<StateRestoreIssue> issues{};
        };

        struct StateSnapshot final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            std::string className{};
            CueScriptStateDescriptor stateDescriptor{};
            bool hasStateDescriptor = false;
            std::vector<std::byte> bytes{};
        };

        explicit ScriptRuntime(
            GameCore::GameWorld& a_gameWorld,
            PAL::IPlatform* a_platform = nullptr) noexcept;
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
        [[nodiscard]] const MarionnetteClass* find_marionnette_class(
            std::string_view a_className) const noexcept;
        [[nodiscard]] const MarionnetteClass* find_marionnette_class(
            GameCore::EntityId a_entityId) const noexcept;
        [[nodiscard]] Marionnette* find_marionnette(
            GameCore::EntityId a_entityId) noexcept;
        [[nodiscard]] const Marionnette* find_marionnette(
            GameCore::EntityId a_entityId) const noexcept;
        [[nodiscard]] MarionnetteComponent* find_marionnette_component(
            GameCore::EntityId a_entityId) noexcept;
        [[nodiscard]] const MarionnetteComponent* find_marionnette_component(
            GameCore::EntityId a_entityId) const noexcept;
        [[nodiscard]] MarionnetteComponent* find_marionnette_component_by_class(
            GameCore::EntityId a_entityId,
            const MarionnetteClass* a_componentClass) noexcept;
        [[nodiscard]] const MarionnetteComponent*
            find_marionnette_component_by_class(
                GameCore::EntityId a_entityId,
                const MarionnetteClass* a_componentClass) const noexcept;
        void activate() noexcept;
        void set_module(const ScriptModule* a_module) noexcept;
        [[nodiscard]] Result set_game_world(GameCore::GameWorld& a_gameWorld) noexcept;
        [[nodiscard]] Result capture_instance_states(
            std::vector<StateSnapshot>& a_outSnapshots) const noexcept;
        [[nodiscard]] Result restore_instance_states(
            const std::vector<StateSnapshot>& a_snapshots,
            StateRestoreReport* a_outReport = nullptr) noexcept;

        [[nodiscard]] Result update(float a_deltaTimeSeconds) noexcept;
        [[nodiscard]] Result reset() noexcept;

    private:
        struct ScriptBinding final
        {
            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            std::string className{};
            std::vector<ECS::ScriptFieldValue> fieldValues{};
            MarionnetteObject* scriptObject = nullptr;
        };

        struct ScriptClassInfo final
        {
            std::string className{};
            std::vector<ECS::ScriptFieldValue> fieldDefaults{};
            std::vector<MarionnetteProperty> properties{};
            std::vector<std::string> functionNames{};
            std::vector<MarionnetteFunction> functions{};
            MarionnetteClass marionnetteClass{};
        };

        [[nodiscard]] Result sync_instances() noexcept;
        [[nodiscard]] Result create_instance(
            GameCore::EntityId a_entityId,
            const ECS::ScriptComponent& a_scriptComponent) noexcept;
        [[nodiscard]] Result destroy_instance(GameCore::EntityId a_entityId) noexcept;
        [[nodiscard]] Result destroy_all_instances() noexcept;
        [[nodiscard]] std::vector<ECS::ScriptFieldValue> resolve_script_field_values(
            const ECS::ScriptComponent& a_scriptComponent) const;
        void refresh_marionnette_class_info(ScriptClassInfo& a_classInfo) noexcept;
        [[nodiscard]] Result bind_marionnette(
            GameCore::EntityId a_entityId) noexcept;
        [[nodiscard]] Result bind_marionnette_component(
            GameCore::EntityId a_entityId,
            const ECS::ScriptComponent& a_scriptComponent) noexcept;
        void unbind_marionnette(GameCore::EntityId a_entityId) noexcept;
        void unbind_marionnette_component(GameCore::EntityId a_entityId) noexcept;

        [[nodiscard]] static CueResult CUE_SCRIPT_CALL log_bridge(
            CueLogSeverity a_severity,
            CueStringView a_message);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL register_script_class_bridge(
            CueStringView a_scriptClassName);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL register_script_field_bridge(
            CueStringView a_scriptClassName,
            const CueScriptFieldValue* a_fieldValue);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL register_script_function_bridge(
            CueStringView a_scriptClassName,
            const CueScriptFunctionDefinition* a_functionDefinition);
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
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL push_key_bridge(
            CueKey a_key);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL find_script_instance_bridge(
            CueEntityHandle a_entityHandle,
            CueStringView a_scriptClassName,
            CueScriptInstanceHandle* a_outInstanceHandle);
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL is_script_instance_valid_bridge(
            CueScriptInstanceHandle a_instanceHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL get_script_field_bridge(
            CueScriptInstanceHandle a_instanceHandle,
            CueStringView a_fieldName,
            CueScriptFieldValue* a_outFieldValue);
        [[nodiscard]] static void* CUE_SCRIPT_CALL get_script_object_bridge(
            CueScriptInstanceHandle a_instanceHandle);
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL is_script_class_registered_bridge(
            CueStringView a_scriptClassName);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL get_script_class_field_bridge(
            CueStringView a_scriptClassName,
            CueStringView a_fieldName,
            CueScriptFieldValue* a_outFieldValue);
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL has_script_class_function_bridge(
            CueStringView a_scriptClassName,
            CueStringView a_functionName);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL invoke_script_function_bridge(
            CueScriptInstanceHandle a_instanceHandle,
            CueStringView a_functionName);

        [[nodiscard]] CueResult log_internal(
            CueLogSeverity a_severity,
            CueStringView a_message) noexcept;
        [[nodiscard]] CueResult register_script_class_internal(
            CueStringView a_scriptClassName) noexcept;
        [[nodiscard]] CueResult register_script_field_internal(
            CueStringView a_scriptClassName,
            const CueScriptFieldValue* a_fieldValue) noexcept;
        [[nodiscard]] CueResult register_script_function_internal(
            CueStringView a_scriptClassName,
            const CueScriptFunctionDefinition* a_functionDefinition) noexcept;
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
        [[nodiscard]] uint8_t push_key_internal(CueKey a_key) const noexcept;
        [[nodiscard]] CueResult find_script_instance_internal(
            CueEntityHandle a_entityHandle,
            CueStringView a_scriptClassName,
            CueScriptInstanceHandle* a_outInstanceHandle) const noexcept;
        [[nodiscard]] uint8_t is_script_instance_valid_internal(
            CueScriptInstanceHandle a_instanceHandle) const noexcept;
        [[nodiscard]] CueResult get_script_field_internal(
            CueScriptInstanceHandle a_instanceHandle,
            CueStringView a_fieldName,
            CueScriptFieldValue* a_outFieldValue) const noexcept;
        [[nodiscard]] uint8_t is_script_class_registered_internal(
            CueStringView a_scriptClassName) const noexcept;
        [[nodiscard]] CueResult get_script_class_field_internal(
            CueStringView a_scriptClassName,
            CueStringView a_fieldName,
            CueScriptFieldValue* a_outFieldValue) const noexcept;
        [[nodiscard]] uint8_t has_script_class_function_internal(
            CueStringView a_scriptClassName,
            CueStringView a_functionName) const noexcept;
        [[nodiscard]] CueResult invoke_script_function_internal(
            CueScriptInstanceHandle a_instanceHandle,
            CueStringView a_functionName) const noexcept;
        [[nodiscard]] MarionnetteObject* get_script_object_internal(
            CueScriptInstanceHandle a_instanceHandle) const noexcept;

        [[nodiscard]] static Result convert_script_result(CueResult a_result) noexcept;
        [[nodiscard]] static CueResult convert_result_code(const Result& a_result) noexcept;
        [[nodiscard]] static GameCore::EntityId to_entity_id(
            CueEntityHandle a_entityHandle) noexcept;
        [[nodiscard]] static CueEntityHandle to_entity_handle(
            GameCore::EntityId a_entityId) noexcept;

        static ScriptRuntime* s_activeInstance;

        GameCore::GameWorld* m_gameWorld = nullptr;
        PAL::IPlatform* m_platform = nullptr;
        const ScriptModule* m_module = nullptr;
        CueEngineApi m_engineApi{};
        std::unordered_map<GameCore::EntityId, ScriptBinding> m_bindings{};
        std::unordered_map<GameCore::EntityId, Marionnette> m_marionnettes{};
        std::unordered_map<GameCore::EntityId, std::unique_ptr<MarionnetteComponent>>
            m_marionnetteComponents{};
        std::unordered_map<uint64_t, GameCore::EntityId> m_entityIdsByInstanceHandle{};
        std::vector<std::string> m_registeredScriptClasses{};
        std::unordered_map<std::string, ScriptClassInfo> m_scriptClassInfos{};
    };
}
