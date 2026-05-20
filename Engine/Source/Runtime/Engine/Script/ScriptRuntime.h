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
        [[nodiscard]] Result dispatch_collision_events() noexcept;
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
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            get_transform_degrees_bridge(
                CueEntityHandle a_entityHandle,
                CueTransformData* a_outTransform);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_transform_degrees_bridge(
                CueEntityHandle a_entityHandle,
                const CueTransformData* a_transform);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            get_transform_quaternion_bridge(
                CueEntityHandle a_entityHandle,
                CueTransformQuaternionData* a_outTransform);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_transform_quaternion_bridge(
                CueEntityHandle a_entityHandle,
                const CueTransformQuaternionData* a_transform);
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
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL request_audio_source_play_bridge(
            CueEntityHandle a_entityHandle);
        [[nodiscard]] static CueSceneId CUE_SCRIPT_CALL request_scene_load_bridge(
            CueStringView a_sceneName);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL request_scene_unload_bridge(
            CueSceneId a_sceneId);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_rigid_body_linear_velocity_bridge(
                CueEntityHandle a_entityHandle,
                const CueFloat3* a_velocity);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            get_rigid_body_linear_velocity_bridge(
                CueEntityHandle a_entityHandle,
                CueFloat3* a_outVelocity);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            add_rigid_body_force_bridge(
                CueEntityHandle a_entityHandle,
                const CueFloat3* a_force);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            add_rigid_body_impulse_bridge(
                CueEntityHandle a_entityHandle,
                const CueFloat3* a_impulse);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_character_move_velocity_bridge(
                CueEntityHandle a_entityHandle,
                const CueFloat3* a_velocity);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            request_character_jump_bridge(CueEntityHandle a_entityHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_nav_agent_destination_bridge(
                CueEntityHandle a_entityHandle,
                const CueFloat3* a_destination);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_nav_agent_target_bridge(
                CueEntityHandle a_entityHandle,
                CueEntityHandle a_targetEntityHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            get_mouse_delta_bridge(CueMouseDeltaData* a_outDelta);
        [[nodiscard]] static uint8_t CUE_SCRIPT_CALL
            push_mouse_button_bridge(CueMouseButton a_button);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            raycast_bridge(
                const CueRaycastDesc* a_desc,
                CueRaycastHit* a_outHit);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            debug_draw_line_bridge(
                const CueFloat3* a_start,
                const CueFloat3* a_end,
                const CueFloat4* a_color,
                float a_durationSeconds);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            debug_draw_sphere_bridge(
                const CueFloat3* a_center,
                float a_radius,
                const CueFloat4* a_color,
                float a_durationSeconds);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            debug_draw_box_bridge(
                const CueFloat3* a_center,
                const CueFloat3* a_halfExtent,
                const CueFloat4* a_color,
                float a_durationSeconds);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            spawn_object_bridge(
                const CueSpawnObjectDesc* a_desc,
                CueEntityHandle* a_outEntityHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            instantiate_entity_bridge(
                const CueInstantiateEntityDesc* a_desc,
                CueEntityHandle* a_outEntityHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            find_entities_by_tag_bridge(
                CueStringView a_tag,
                CueEntityHandle* a_outEntityHandles,
                uint32_t a_capacity,
                uint32_t* a_outCount);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            find_entities_by_name_bridge(
                CueStringView a_name,
                CueEntityHandle* a_outEntityHandles,
                uint32_t a_capacity,
                uint32_t* a_outCount);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            trigger_overlaps_bridge(
                CueEntityHandle a_triggerEntity,
                CueEntityHandle* a_outEntityHandles,
                uint32_t a_capacity,
                uint32_t* a_outCount);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            sphere_overlap_bridge(
                const CueSphereOverlapDesc* a_desc,
                CueEntityHandle* a_outEntityHandles,
                uint32_t a_capacity,
                uint32_t* a_outCount);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            destroy_entity_bridge(CueEntityHandle a_entityHandle);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            get_camera_fov_y_bridge(
                CueEntityHandle a_entityHandle,
                float* a_outFovY);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            set_camera_fov_y_bridge(
                CueEntityHandle a_entityHandle,
                float a_fovY);
        [[nodiscard]] static CueResult CUE_SCRIPT_CALL
            add_or_set_component_bridge(
                CueEntityHandle a_entityHandle,
                CueComponentKind a_componentKind,
                const void* a_componentData,
                uint32_t a_componentDataSize);

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
        [[nodiscard]] CueResult get_transform_degrees_internal(
            CueEntityHandle a_entityHandle,
            CueTransformData* a_outTransform) const noexcept;
        [[nodiscard]] CueResult set_transform_degrees_internal(
            CueEntityHandle a_entityHandle,
            const CueTransformData* a_transform) noexcept;
        [[nodiscard]] CueResult get_transform_quaternion_internal(
            CueEntityHandle a_entityHandle,
            CueTransformQuaternionData* a_outTransform) const noexcept;
        [[nodiscard]] CueResult set_transform_quaternion_internal(
            CueEntityHandle a_entityHandle,
            const CueTransformQuaternionData* a_transform) noexcept;
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
        [[nodiscard]] CueResult request_audio_source_play_internal(
            CueEntityHandle a_entityHandle) noexcept;
        [[nodiscard]] CueSceneId request_scene_load_internal(
            CueStringView a_sceneName) noexcept;
        [[nodiscard]] CueResult request_scene_unload_internal(
            CueSceneId a_sceneId) noexcept;
        [[nodiscard]] CueResult set_rigid_body_linear_velocity_internal(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_velocity) noexcept;
        [[nodiscard]] CueResult get_rigid_body_linear_velocity_internal(
            CueEntityHandle a_entityHandle,
            CueFloat3* a_outVelocity) const noexcept;
        [[nodiscard]] CueResult add_rigid_body_force_internal(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_force) noexcept;
        [[nodiscard]] CueResult add_rigid_body_impulse_internal(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_impulse) noexcept;
        [[nodiscard]] CueResult set_character_move_velocity_internal(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_velocity) noexcept;
        [[nodiscard]] CueResult request_character_jump_internal(
            CueEntityHandle a_entityHandle) noexcept;
        [[nodiscard]] CueResult set_nav_agent_destination_internal(
            CueEntityHandle a_entityHandle,
            const CueFloat3* a_destination) noexcept;
        [[nodiscard]] CueResult set_nav_agent_target_internal(
            CueEntityHandle a_entityHandle,
            CueEntityHandle a_targetEntityHandle) noexcept;
        [[nodiscard]] CueResult get_mouse_delta_internal(
            CueMouseDeltaData* a_outDelta) const noexcept;
        [[nodiscard]] uint8_t push_mouse_button_internal(
            CueMouseButton a_button) const noexcept;
        [[nodiscard]] CueResult raycast_internal(
            const CueRaycastDesc* a_desc,
            CueRaycastHit* a_outHit) const noexcept;
        [[nodiscard]] CueResult debug_draw_line_internal(
            const CueFloat3* a_start,
            const CueFloat3* a_end,
            const CueFloat4* a_color,
            float a_durationSeconds) noexcept;
        [[nodiscard]] CueResult debug_draw_sphere_internal(
            const CueFloat3* a_center,
            float a_radius,
            const CueFloat4* a_color,
            float a_durationSeconds) noexcept;
        [[nodiscard]] CueResult debug_draw_box_internal(
            const CueFloat3* a_center,
            const CueFloat3* a_halfExtent,
            const CueFloat4* a_color,
            float a_durationSeconds) noexcept;
        [[nodiscard]] CueResult spawn_object_internal(
            const CueSpawnObjectDesc* a_desc,
            CueEntityHandle* a_outEntityHandle) noexcept;
        [[nodiscard]] CueResult instantiate_entity_internal(
            const CueInstantiateEntityDesc* a_desc,
            CueEntityHandle* a_outEntityHandle) noexcept;
        [[nodiscard]] CueResult find_entities_by_tag_internal(
            CueStringView a_tag,
            CueEntityHandle* a_outEntityHandles,
            uint32_t a_capacity,
            uint32_t* a_outCount) noexcept;
        [[nodiscard]] CueResult find_entities_by_name_internal(
            CueStringView a_name,
            CueEntityHandle* a_outEntityHandles,
            uint32_t a_capacity,
            uint32_t* a_outCount) noexcept;
        [[nodiscard]] CueResult trigger_overlaps_internal(
            CueEntityHandle a_triggerEntity,
            CueEntityHandle* a_outEntityHandles,
            uint32_t a_capacity,
            uint32_t* a_outCount) noexcept;
        [[nodiscard]] CueResult sphere_overlap_internal(
            const CueSphereOverlapDesc* a_desc,
            CueEntityHandle* a_outEntityHandles,
            uint32_t a_capacity,
            uint32_t* a_outCount) noexcept;
        [[nodiscard]] CueResult destroy_entity_internal(
            CueEntityHandle a_entityHandle) noexcept;
        [[nodiscard]] CueResult get_camera_fov_y_internal(
            CueEntityHandle a_entityHandle,
            float* a_outFovY) const noexcept;
        [[nodiscard]] CueResult set_camera_fov_y_internal(
            CueEntityHandle a_entityHandle,
            float a_fovY) noexcept;
        [[nodiscard]] CueResult add_or_set_component_internal(
            CueEntityHandle a_entityHandle,
            CueComponentKind a_componentKind,
            const void* a_componentData,
            uint32_t a_componentDataSize) noexcept;

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
