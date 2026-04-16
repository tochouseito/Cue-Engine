#include "ScriptRuntime.h"

// === Core includes ===
#include <IO/Logger.h>

// === Engine includes ===
#include "../GameCore/Components.h"
#include "../GameCore/GameObject.h"
#include "../GameCore/GameWorld.h"
#include "ScriptModule.h"

// === C++ includes ===
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Cue
{
    namespace
    {
        [[nodiscard]] std::string_view to_string_view(CueStringView a_value) noexcept
        {
            return a_value.data != nullptr
                ? std::string_view(a_value.data, a_value.size)
                : std::string_view{};
        }

        [[nodiscard]] CueStringView make_string_view(std::string_view a_value) noexcept
        {
            return CueStringView{
                a_value.data(),
                static_cast<uint32_t>(a_value.size())
            };
        }

        [[nodiscard]] ECS::ScriptFieldType to_script_field_type(
            CueScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case CueScriptFieldType_Int32:
                return ECS::ScriptFieldType::Int32;
            case CueScriptFieldType_Bool:
                return ECS::ScriptFieldType::Bool;
            case CueScriptFieldType_Float:
            default:
                return ECS::ScriptFieldType::Float;
            }
        }

        [[nodiscard]] CueScriptFieldType to_cue_script_field_type(
            ECS::ScriptFieldType a_type) noexcept
        {
            switch (a_type)
            {
            case ECS::ScriptFieldType::Int32:
                return CueScriptFieldType_Int32;
            case ECS::ScriptFieldType::Bool:
                return CueScriptFieldType_Bool;
            case ECS::ScriptFieldType::Float:
            default:
                return CueScriptFieldType_Float;
            }
        }

        [[nodiscard]] bool are_script_field_values_equal(
            const std::vector<ECS::ScriptFieldValue>& a_left,
            const std::vector<ECS::ScriptFieldValue>& a_right) noexcept
        {
            if (a_left.size() != a_right.size())
            {
                return false;
            }

            for (size_t index = 0; index < a_left.size(); ++index)
            {
                const ECS::ScriptFieldValue& left = a_left[index];
                const ECS::ScriptFieldValue& right = a_right[index];
                if (left.name != right.name ||
                    left.type != right.type ||
                    left.floatValue != right.floatValue ||
                    left.intValue != right.intValue ||
                    left.boolValue != right.boolValue)
                {
                    return false;
                }
            }

            return true;
        }
    }

    ScriptRuntime* ScriptRuntime::s_activeInstance = nullptr;

    ScriptRuntime::ScriptRuntime(GameCore::GameWorld& a_gameWorld) noexcept
        : m_gameWorld(a_gameWorld)
    {
        s_activeInstance = this;
        m_engineApi.structSize = sizeof(CueEngineApi);
        m_engineApi.abiVersion = k_cueScriptAbiVersion;
        m_engineApi.log = &ScriptRuntime::log_bridge;
        m_engineApi.registerScriptClass = &ScriptRuntime::register_script_class_bridge;
        m_engineApi.isEntityValid = &ScriptRuntime::is_entity_valid_bridge;
        m_engineApi.hasTransform = &ScriptRuntime::has_transform_bridge;
        m_engineApi.getTransform = &ScriptRuntime::get_transform_bridge;
        m_engineApi.setTransform = &ScriptRuntime::set_transform_bridge;
        m_engineApi.registerScriptField = &ScriptRuntime::register_script_field_bridge;
    }

    ScriptRuntime::~ScriptRuntime()
    {
        (void)reset();
        if (s_activeInstance == this)
        {
            s_activeInstance = nullptr;
        }
    }

    const CueEngineApi& ScriptRuntime::engine_api() const noexcept
    {
        return m_engineApi;
    }

    const std::vector<std::string>&
        ScriptRuntime::registered_script_classes() const noexcept
    {
        return m_registeredScriptClasses;
    }

    bool ScriptRuntime::has_registered_script_class(
        std::string_view a_className) const noexcept
    {
        return std::find(m_registeredScriptClasses.begin(),
                   m_registeredScriptClasses.end(),
                   a_className) != m_registeredScriptClasses.end();
    }

    const std::vector<ECS::ScriptFieldValue>&
        ScriptRuntime::script_field_defaults(std::string_view a_className) const noexcept
    {
        static const std::vector<ECS::ScriptFieldValue> k_emptyFieldValues{};

        const auto infoIt = m_scriptClassInfos.find(std::string(a_className));
        if (infoIt == m_scriptClassInfos.end())
        {
            return k_emptyFieldValues;
        }

        return infoIt->second.fieldDefaults;
    }

    void ScriptRuntime::activate() noexcept
    {
        s_activeInstance = this;
    }

    void ScriptRuntime::set_module(const ScriptModule* a_module) noexcept
    {
        m_module = a_module;
        m_registeredScriptClasses.clear();
        m_scriptClassInfos.clear();
    }

    Result ScriptRuntime::update(float a_deltaTimeSeconds) noexcept
    {
        Result result = m_gameWorld.execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        result = sync_instances();
        if (!result)
        {
            return result;
        }

        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (exports == nullptr || exports->updateScriptInstance == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime module exports are invalid.");
        }

        std::vector<GameCore::EntityId> staleEntities{};
        staleEntities.reserve(m_bindings.size());

        for (const auto& [entityId, binding] : m_bindings)
        {
            const CueResult updateResult =
                exports->updateScriptInstance(binding.instanceHandle, a_deltaTimeSeconds);
            if (updateResult == CueResult_Ok)
            {
                continue;
            }

            if (updateResult == CueResult_NotFound)
            {
                staleEntities.push_back(entityId);
                continue;
            }

            return convert_script_result(updateResult);
        }

        for (const GameCore::EntityId entityId : staleEntities)
        {
            result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::reset() noexcept
    {
        return destroy_all_instances();
    }

    Result ScriptRuntime::sync_instances() noexcept
    {
        std::unordered_set<GameCore::EntityId> desiredEntities{};
        desiredEntities.reserve(m_bindings.size() + 8);
        Result syncResult = Result::ok();

        Result enumerateResult = m_gameWorld.for_each_object(
            [this, &desiredEntities, &syncResult](GameCore::EntityId a_entityId,
                GameCore::SceneId, GameCore::GameObject& a_object)
            {
                if (!syncResult)
                {
                    return;
                }

                bool hasScript = false;
                Result hasResult =
                    a_object.has_component<ECS::ScriptComponent>(hasScript);
                if (!hasResult || !hasScript)
                {
                    return;
                }

                ECS::ScriptComponent* script = nullptr;
                Result getResult = a_object.get_component(script);
                if (!getResult || script == nullptr || !script->isEnabled ||
                    script->className.empty())
                {
                    return;
                }

                desiredEntities.insert(a_entityId);
                const std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
                    resolve_script_field_values(*script);

                const auto bindingIt = m_bindings.find(a_entityId);
                if (bindingIt != m_bindings.end() &&
                    bindingIt->second.className == script->className &&
                    are_script_field_values_equal(
                        bindingIt->second.fieldValues,
                        resolvedFieldValues))
                {
                    return;
                }

                if (bindingIt != m_bindings.end())
                {
                    syncResult = destroy_instance(a_entityId);
                    if (!syncResult)
                    {
                        return;
                    }
                }

                syncResult = create_instance(
                    a_entityId, *script);
                if (!syncResult)
                {
                    return;
                }
            });
        if (!enumerateResult)
        {
            return enumerateResult;
        }
        if (!syncResult)
        {
            return syncResult;
        }

        std::vector<GameCore::EntityId> removedEntities{};
        removedEntities.reserve(m_bindings.size());
        for (const auto& [entityId, _] : m_bindings)
        {
            if (!desiredEntities.contains(entityId))
            {
                removedEntities.push_back(entityId);
            }
        }

        for (const GameCore::EntityId entityId : removedEntities)
        {
            Result result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::create_instance(
        GameCore::EntityId a_entityId,
        const ECS::ScriptComponent& a_scriptComponent) noexcept
    {
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::ok();
        }

        const CueScriptExports* exports = m_module->exports();
        if (exports == nullptr || exports->createScriptInstance == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime module exports are invalid.");
        }

        CueScriptCreateInfo createInfo{};
        createInfo.entityHandle = to_entity_handle(a_entityId);
        createInfo.scriptName = make_string_view(a_scriptComponent.className);

        std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
            resolve_script_field_values(a_scriptComponent);

        std::vector<CueScriptFieldValue> fieldValues{};
        fieldValues.reserve(resolvedFieldValues.size());
        for (const ECS::ScriptFieldValue& fieldValue : resolvedFieldValues)
        {
            fieldValues.push_back(CueScriptFieldValue{
                make_string_view(fieldValue.name),
                to_cue_script_field_type(fieldValue.type),
                fieldValue.floatValue,
                fieldValue.intValue,
                static_cast<uint8_t>(fieldValue.boolValue ? 1 : 0),
                0,
                0,
                0
            });
        }
        createInfo.fieldValues = fieldValues.empty() ? nullptr : fieldValues.data();
        createInfo.fieldValueCount = static_cast<uint32_t>(fieldValues.size());

        CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
        const CueResult createResult =
            exports->createScriptInstance(&createInfo, &instanceHandle);
        Result result = convert_script_result(createResult);
        if (!result)
        {
            return result;
        }

        m_bindings[a_entityId] = ScriptBinding{
            instanceHandle,
            a_scriptComponent.className,
            resolvedFieldValues
        };
        return Result::ok();
    }

    std::vector<ECS::ScriptFieldValue> ScriptRuntime::resolve_script_field_values(
        const ECS::ScriptComponent& a_scriptComponent) const
    {
        std::vector<ECS::ScriptFieldValue> resolvedFieldValues =
            script_field_defaults(a_scriptComponent.className);
        for (const ECS::ScriptFieldValue& fieldValue : a_scriptComponent.fieldValues)
        {
            const auto existingIt =
                std::find_if(resolvedFieldValues.begin(), resolvedFieldValues.end(),
                    [&fieldValue](const ECS::ScriptFieldValue& a_existingValue)
                    {
                        return a_existingValue.name == fieldValue.name;
                    });
            if (existingIt != resolvedFieldValues.end())
            {
                *existingIt = fieldValue;
                continue;
            }

            resolvedFieldValues.push_back(fieldValue);
        }

        return resolvedFieldValues;
    }

    Result ScriptRuntime::destroy_instance(GameCore::EntityId a_entityId) noexcept
    {
        const auto bindingIt = m_bindings.find(a_entityId);
        if (bindingIt == m_bindings.end())
        {
            return Result::ok();
        }

        if (m_module != nullptr && m_module->is_loaded())
        {
            const CueScriptExports* exports = m_module->exports();
            if (exports == nullptr || exports->destroyScriptInstance == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Script runtime module exports are invalid.");
            }

            const CueResult destroyResult =
                exports->destroyScriptInstance(bindingIt->second.instanceHandle);
            Result result = convert_script_result(destroyResult);
            if (!result && result.code != Code::NotFound)
            {
                return result;
            }
        }

        m_bindings.erase(bindingIt);
        return Result::ok();
    }

    Result ScriptRuntime::destroy_all_instances() noexcept
    {
        std::vector<GameCore::EntityId> entityIds{};
        entityIds.reserve(m_bindings.size());
        for (const auto& [entityId, _] : m_bindings)
        {
            entityIds.push_back(entityId);
        }

        for (const GameCore::EntityId entityId : entityIds)
        {
            Result result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::log_bridge(
        CueLogSeverity a_severity,
        CueStringView a_message)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->log_internal(a_severity, a_message)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::register_script_class_bridge(
        CueStringView a_scriptClassName)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->register_script_class_internal(a_scriptClassName)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::register_script_field_bridge(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->register_script_field_internal(
                a_scriptClassName, a_fieldValue)
            : CueResult_InvalidState;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::is_entity_valid_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->is_entity_valid_internal(a_entityHandle)
            : 0;
    }

    uint8_t CUE_SCRIPT_CALL ScriptRuntime::has_transform_bridge(
        CueEntityHandle a_entityHandle)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->has_transform_internal(a_entityHandle)
            : 0;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::get_transform_bridge(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->get_transform_internal(
                a_entityHandle, a_outTransform)
            : CueResult_InvalidState;
    }

    CueResult CUE_SCRIPT_CALL ScriptRuntime::set_transform_bridge(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform)
    {
        return s_activeInstance != nullptr
            ? s_activeInstance->set_transform_internal(
                a_entityHandle, a_transform)
            : CueResult_InvalidState;
    }

    CueResult ScriptRuntime::log_internal(
        CueLogSeverity a_severity,
        CueStringView a_message) noexcept
    {
        const std::string_view message =
            a_message.data != nullptr
            ? std::string_view(a_message.data, a_message.size)
            : std::string_view{};

        switch (a_severity)
        {
        case CueLogSeverity_Info:
            Core::IO::log(Core::IO::LogSink::debugConsole, "[Script] {}", message);
            return CueResult_Ok;

        case CueLogSeverity_Warning:
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "[Script][Warning] {}", message);
            return CueResult_Ok;

        case CueLogSeverity_Error:
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "[Script][Error] {}", message);
            return CueResult_Ok;
        }

        return CueResult_InvalidArgument;
    }

    CueResult ScriptRuntime::register_script_class_internal(
        CueStringView a_scriptClassName) noexcept
    {
        if (a_scriptClassName.data == nullptr || a_scriptClassName.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const std::string className(
            a_scriptClassName.data, a_scriptClassName.size);
        if (!m_scriptClassInfos.contains(className))
        {
            m_scriptClassInfos.emplace(className, ScriptClassInfo{ className, {} });
        }
        if (has_registered_script_class(className))
        {
            return CueResult_Ok;
        }

        m_registeredScriptClasses.push_back(className);
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::register_script_field_internal(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue) noexcept
    {
        if (a_fieldValue == nullptr ||
            a_scriptClassName.data == nullptr ||
            a_scriptClassName.size == 0 ||
            a_fieldValue->name.data == nullptr ||
            a_fieldValue->name.size == 0)
        {
            return CueResult_InvalidArgument;
        }

        const std::string className(
            a_scriptClassName.data, a_scriptClassName.size);
        const CueResult registerClassResult =
            register_script_class_internal(a_scriptClassName);
        if (registerClassResult != CueResult_Ok)
        {
            return registerClassResult;
        }

        auto infoIt = m_scriptClassInfos.find(className);
        if (infoIt == m_scriptClassInfos.end())
        {
            return CueResult_InternalError;
        }

        ECS::ScriptFieldValue nextFieldValue{};
        nextFieldValue.name.assign(
            a_fieldValue->name.data,
            a_fieldValue->name.size);
        nextFieldValue.type = to_script_field_type(a_fieldValue->type);
        nextFieldValue.floatValue = a_fieldValue->floatValue;
        nextFieldValue.intValue = a_fieldValue->intValue;
        nextFieldValue.boolValue = a_fieldValue->boolValue != 0;

        std::vector<ECS::ScriptFieldValue>& fieldDefaults =
            infoIt->second.fieldDefaults;
        const auto existingIt =
            std::find_if(fieldDefaults.begin(), fieldDefaults.end(),
                [&nextFieldValue](const ECS::ScriptFieldValue& a_value)
                {
                    return a_value.name == nextFieldValue.name;
                });
        if (existingIt != fieldDefaults.end())
        {
            *existingIt = std::move(nextFieldValue);
            return CueResult_Ok;
        }

        fieldDefaults.push_back(std::move(nextFieldValue));
        return CueResult_Ok;
    }

    uint8_t ScriptRuntime::is_entity_valid_internal(
        CueEntityHandle a_entityHandle) const noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return 0;
        }

        bool containsObject = false;
        const Result result = m_gameWorld.contains_object(
            to_entity_id(a_entityHandle), containsObject);
        return result && containsObject ? 1 : 0;
    }

    uint8_t ScriptRuntime::has_transform_internal(
        CueEntityHandle a_entityHandle) const noexcept
    {
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return 0;
        }

        bool hasTransform = false;
        const Result result = m_gameWorld.has_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), hasTransform);
        return result && hasTransform ? 1 : 0;
    }

    CueResult ScriptRuntime::get_transform_internal(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform) const noexcept
    {
        if (a_outTransform == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }

        ECS::TransformComponent* transform = nullptr;
        const Result result = m_gameWorld.get_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), transform);
        if (!result || transform == nullptr)
        {
            return convert_result_code(result);
        }

        a_outTransform->position =
            { transform->position.x, transform->position.y, transform->position.z };
        a_outTransform->rotation =
            { transform->rotation.x, transform->rotation.y, transform->rotation.z };
        a_outTransform->scale =
            { transform->scale.x, transform->scale.y, transform->scale.z };
        return CueResult_Ok;
    }

    CueResult ScriptRuntime::set_transform_internal(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform) noexcept
    {
        if (a_transform == nullptr)
        {
            return CueResult_InvalidArgument;
        }
        if (a_entityHandle.value == k_cueInvalidHandleValue)
        {
            return CueResult_InvalidArgument;
        }

        ECS::TransformComponent* transform = nullptr;
        const Result result = m_gameWorld.get_component<ECS::TransformComponent>(
            to_entity_id(a_entityHandle), transform);
        if (!result || transform == nullptr)
        {
            return convert_result_code(result);
        }

        transform->position =
            Math::float3(a_transform->position.x, a_transform->position.y,
                a_transform->position.z);
        transform->rotation =
            Math::float3(a_transform->rotation.x, a_transform->rotation.y,
                a_transform->rotation.z);
        transform->scale =
            Math::float3(a_transform->scale.x, a_transform->scale.y,
                a_transform->scale.z);
        return CueResult_Ok;
    }

    Result ScriptRuntime::convert_script_result(CueResult a_result) noexcept
    {
        switch (a_result)
        {
        case CueResult_Ok:
            return Result::ok();

        case CueResult_InvalidArgument:
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script runtime returned InvalidArgument.");

        case CueResult_NotFound:
            return Result::fail(Code::NotFound, Severity::Error,
                "Script runtime returned NotFound.");

        case CueResult_Unsupported:
            return Result::fail(Code::Unsupported, Severity::Error,
                "Script runtime returned Unsupported.");

        case CueResult_InvalidState:
            return Result::fail(Code::InvalidState, Severity::Error,
                "Script runtime returned InvalidState.");

        case CueResult_InternalError:
            return Result::fail(Code::InternalError, Severity::Error,
                "Script runtime returned InternalError.");
        }

        return Result::fail(Code::UnknownError, Severity::Error,
            "Script runtime returned an unknown result code.");
    }

    CueResult ScriptRuntime::convert_result_code(const Result& a_result) noexcept
    {
        switch (a_result.code)
        {
        case Code::OK:
            return CueResult_Ok;

        case Code::InvalidArgument:
            return CueResult_InvalidArgument;

        case Code::NotFound:
            return CueResult_NotFound;

        case Code::Unsupported:
            return CueResult_Unsupported;

        case Code::InvalidState:
            return CueResult_InvalidState;

        case Code::InternalError:
        case Code::UnknownError:
        case Code::InitializeFailed:
        case Code::CreateFailed:
        case Code::GetFailed:
        case Code::OutOfMemory:
        case Code::AccessDenied:
        case Code::DeviceLost:
            return CueResult_InternalError;
        }

        return CueResult_InternalError;
    }

    GameCore::EntityId ScriptRuntime::to_entity_id(
        CueEntityHandle a_entityHandle) noexcept
    {
        return static_cast<GameCore::EntityId>(a_entityHandle.value);
    }

    CueEntityHandle ScriptRuntime::to_entity_handle(
        GameCore::EntityId a_entityId) noexcept
    {
        return CueEntityHandle{ static_cast<uint64_t>(a_entityId) };
    }
}
