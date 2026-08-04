#include "ScriptRuntime.h"

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameWorld.h"
#include "ScriptModule.h"

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Cue::Script
{
    namespace
    {
        [[nodiscard]] const ECS::ScriptFieldValue* find_field_value(
            const ECS::ScriptComponent& a_component,
            std::string_view a_name) noexcept
        {
            const auto findIn = [a_name](
                                    const std::vector<ECS::ScriptFieldValue>& a_values)
                -> const ECS::ScriptFieldValue*
            {
                const auto value = std::find_if(
                    a_values.begin(), a_values.end(),
                    [a_name](const ECS::ScriptFieldValue& a_field)
                    {
                        return a_field.name == a_name;
                    });
                return value != a_values.end() ? &*value : nullptr;
            };

            const ECS::ScriptFieldValue* value =
                findIn(a_component.serializedFieldValues);
            return value != nullptr ? value
                                    : findIn(a_component.transientFieldValues);
        }

        [[nodiscard]] Core::Native::ScriptFieldValue make_abi_field_value(
            const ScriptFieldInfo& a_fieldInfo,
            const ECS::ScriptFieldValue* a_componentValue) noexcept
        {
            Core::Native::ScriptFieldValue value{};
            value.name = {
                a_fieldInfo.name.data(),
                static_cast<uint32_t>(a_fieldInfo.name.size())};
            value.type = a_fieldInfo.type;
            value.flags = a_fieldInfo.flags;
            value.floatValue = a_fieldInfo.floatValue;
            value.int32Value = a_fieldInfo.int32Value;
            value.boolValue = a_fieldInfo.boolValue ? 1u : 0u;
            value.entityValue = a_fieldInfo.entityValue;
            value.classValue = {
                a_fieldInfo.classValue.data(),
                static_cast<uint32_t>(a_fieldInfo.classValue.size())};
            if (a_componentValue == nullptr)
            {
                return value;
            }

            if (const float* floatValue =
                    std::get_if<float>(&a_componentValue->value);
                floatValue != nullptr &&
                a_fieldInfo.type == Core::Native::ScriptFieldType::Float)
            {
                value.floatValue = *floatValue;
            }
            else if (const int32_t* int32Value =
                         std::get_if<int32_t>(&a_componentValue->value);
                     int32Value != nullptr &&
                     a_fieldInfo.type ==
                         Core::Native::ScriptFieldType::Int32)
            {
                value.int32Value = *int32Value;
            }
            else if (const bool* boolValue =
                         std::get_if<bool>(&a_componentValue->value);
                     boolValue != nullptr &&
                     a_fieldInfo.type == Core::Native::ScriptFieldType::Bool)
            {
                value.boolValue = *boolValue ? 1u : 0u;
            }
            else if (const ECS::ScriptEntityReference* entityValue =
                         std::get_if<ECS::ScriptEntityReference>(
                             &a_componentValue->value);
                     entityValue != nullptr &&
                     a_fieldInfo.type ==
                         Core::Native::ScriptFieldType::Entity)
            {
                value.entityValue = {
                    entityValue->entityId, entityValue->generation};
            }
            else if (const ECS::ScriptReference* scriptValue =
                         std::get_if<ECS::ScriptReference>(
                             &a_componentValue->value);
                     scriptValue != nullptr &&
                     a_fieldInfo.type ==
                         Core::Native::ScriptFieldType::Script)
            {
                value.entityValue = {
                    scriptValue->entity.entityId,
                    scriptValue->entity.generation};
                value.classValue = {
                    scriptValue->className.data(),
                    static_cast<uint32_t>(scriptValue->className.size())};
            }
            return value;
        }
    } // namespace

    ScriptRuntime::ScriptRuntime(GameCore::GameWorld& a_world) noexcept
        : m_world(a_world)
    {
        // DLL callback は Runtime ごとに異なる World を参照するため、global state を介さず userData に自身を保持する
        m_scriptEngineApi.structSize = sizeof(Core::Native::ScriptEngineApi);
        m_scriptEngineApi.userData = this;
        m_scriptEngineApi.isEntityValid = &ScriptRuntime::script_is_entity_valid;
        m_scriptEngineApi.readTransform = &ScriptRuntime::script_read_transform;
        m_scriptEngineApi.writeTransform = &ScriptRuntime::script_write_transform;
        m_scriptEngineApi.findInstance = &ScriptRuntime::script_find_instance;
        m_scriptEngineApi.isInstanceValid =
            &ScriptRuntime::script_is_instance_valid;
        m_scriptEngineApi.invokeFunction =
            &ScriptRuntime::script_invoke_function;
    }

    ScriptRuntime::~ScriptRuntime()
    {
        // Engine::shutdown では失敗を報告して reset し、デストラクタは終了経路の最終防御にする
        (void)reset();
    }

    void ScriptRuntime::set_module(const ScriptModule* a_module) noexcept
    {
        // 呼び出し側は reset 後にだけ差し替え、古い DLL の handle を新しい DLL へ渡さない
        m_module = a_module;
    }

    Result ScriptRuntime::start() noexcept
    {
        if (m_module != nullptr && m_module->is_loaded())
        {
            const Result result = m_module->register_engine_api(m_scriptEngineApi);
            if (!result)
            {
                return result;
            }
        }

        return sync_instances(true);
    }

    Result ScriptRuntime::update(float a_deltaTimeSeconds) noexcept
    {
        Result result = sync_instances(true);
        if (!result)
        {
            return result;
        }

        for (auto& [unusedEntityId, binding] : m_bindings)
        {
            (void)unusedEntityId;
            if (!binding.isStarted)
            {
                result = m_module->on_create(binding.instanceHandle);
                if (!result)
                {
                    return result;
                }
                binding.isStarted = true;
            }
            result = m_module->on_update(binding.instanceHandle, a_deltaTimeSeconds);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::reset() noexcept
    {
        std::vector<GameCore::EntityId> entityIds{};
        entityIds.reserve(m_bindings.size());
        for (const auto& [entityId, binding] : m_bindings)
        {
            (void)binding;
            entityIds.push_back(entityId);
        }

        for (const GameCore::EntityId entityId : entityIds)
        {
            const Result result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::capture_instance_states(
        std::vector<StateSnapshot>& a_outSnapshots) const noexcept
    {
        a_outSnapshots.clear();
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return m_bindings.empty()
                       ? Result::ok()
                       : Result::fail(
                             Code::InvalidState, Severity::Error,
                             "Script module was unloaded before state capture.");
        }

        a_outSnapshots.reserve(m_bindings.size());
        for (const auto& [entityId, binding] : m_bindings)
        {
            StateSnapshot snapshot{};
            snapshot.entityId = entityId;
            snapshot.generation = binding.generation;
            snapshot.className = binding.className;

            Result result = m_module->get_state_descriptor(
                binding.className, snapshot.descriptor);
            if (!result)
            {
                return result;
            }

            uint32_t stateSize = 0u;
            result = m_module->get_instance_state_size(
                binding.instanceHandle, stateSize);
            if (!result)
            {
                return result;
            }
            if (stateSize != snapshot.descriptor.size)
            {
                return Result::fail(
                    Code::InvalidState, Severity::Error,
                    "Script state size does not match its descriptor.");
            }

            snapshot.bytes.resize(stateSize);
            result = m_module->serialize_instance(
                binding.instanceHandle, snapshot.bytes.data(), stateSize);
            if (!result)
            {
                return result;
            }
            a_outSnapshots.push_back(std::move(snapshot));
        }

        return Result::ok();
    }

    Result ScriptRuntime::restore_instance_states(
        std::span<const StateSnapshot> a_snapshots) noexcept
    {
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        Result result = m_module->register_engine_api(m_scriptEngineApi);
        if (!result)
        {
            return result;
        }

        result = sync_instances(false);
        if (!result)
        {
            return result;
        }

        for (auto& [entityId, binding] : m_bindings)
        {
            const auto snapshot = std::find_if(
                a_snapshots.begin(), a_snapshots.end(),
                [entityId, &binding](const StateSnapshot& a_value)
                {
                    return a_value.entityId == entityId &&
                           a_value.generation == binding.generation &&
                           a_value.className == binding.className;
                });

            bool restoresState = false;
            if (snapshot != a_snapshots.end())
            {
                Core::Native::ScriptStateDescriptor descriptor{};
                result = m_module->get_state_descriptor(
                    binding.className, descriptor);
                if (!result)
                {
                    return result;
                }

                restoresState =
                    descriptor.version == snapshot->descriptor.version &&
                    descriptor.size == snapshot->descriptor.size &&
                    descriptor.schemaHash == snapshot->descriptor.schemaHash &&
                    snapshot->bytes.size() == descriptor.size;
                if (restoresState)
                {
                    result = m_module->restore_instance(
                        binding.instanceHandle, snapshot->bytes.data(),
                        static_cast<uint32_t>(snapshot->bytes.size()));
                    if (!result)
                    {
                        return result;
                    }
                    binding.isStarted = true;
                }
            }

            if (!restoresState)
            {
                result = m_module->on_create(binding.instanceHandle);
                if (!result)
                {
                    return result;
                }
                binding.isStarted = true;
            }
        }

        return Result::ok();
    }

    const Core::Native::ScriptEngineApi& ScriptRuntime::script_engine_api() const noexcept
    {
        return m_scriptEngineApi;
    }

    Result ScriptRuntime::sync_instances(bool a_startNewInstances) noexcept
    {
        // DLL の exports が未確定な間は class 解決や instance 操作を行わず、空の登録表への binding を防ぐ
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return m_bindings.empty()
                       ? Result::ok()
                       : Result::fail(Code::InvalidState, Severity::Error,
                                      "Script module was unloaded while script instances are still active.");
        }

        struct DesiredInstance final
        {
            std::string className{};
            GameCore::Generation generation = 0u;
        };

        // World を走査中に bindings を変更しないよう、先に現在必要な class の snapshot を作る
        std::unordered_map<GameCore::EntityId, DesiredInstance> desiredInstances{};
        Result collectionResult = Result::ok();
        const Result enumerateResult = m_world.for_each_object(
            [&](GameCore::EntityId a_entityId, GameCore::GameObject a_object)
            {
                if (!collectionResult)
                {
                    return;
                }

                ECS::ScriptComponent* script = nullptr;
                const Result scriptResult = m_world.get_component<ECS::ScriptComponent>(a_entityId, script);
                if (scriptResult.code == Code::NotFound)
                {
                    return;
                }
                if (!scriptResult || script == nullptr)
                {
                    collectionResult = scriptResult;
                    return;
                }

                bool isActive = false;
                const Result activeResult = m_world.is_object_active(a_entityId, isActive);
                if (!activeResult)
                {
                    collectionResult = activeResult;
                    return;
                }

                if (isActive && script->isEnabled && !script->className.empty())
                {
                    // Scene の参照名は復元用に残し、DLL に未登録の間だけ Runtime binding から除外する
                    if (!m_module->has_class(script->className.c_str()))
                    {
                        return;
                    }

                    desiredInstances.emplace(
                        a_entityId,
                        DesiredInstance{script->className, a_object.generation()});
                }
            });
        if (!enumerateResult)
        {
            return enumerateResult;
        }
        if (!collectionResult)
        {
            return collectionResult;
        }

        // 削除・無効化・class 変更済みの Component に対応する instance を先に破棄する
        for (auto iterator = m_bindings.begin(); iterator != m_bindings.end();)
        {
            const auto desired = desiredInstances.find(iterator->first);
            if (desired != desiredInstances.end() &&
                desired->second.className == iterator->second.className &&
                desired->second.generation == iterator->second.generation)
            {
                ++iterator;
                continue;
            }

            const GameCore::EntityId entityId = iterator->first;
            ++iterator;
            const Result result = destroy_instance(entityId);
            if (!result)
            {
                return result;
            }
        }

        if (desiredInstances.empty())
        {
            return Result::ok();
        }

        for (const auto& [entityId, desired] : desiredInstances)
        {
            if (m_bindings.contains(entityId))
            {
                continue;
            }

            const Result result = create_instance(
                entityId, desired.generation, desired.className);
            if (!result)
            {
                return result;
            }
        }

        if (a_startNewInstances)
        {
            for (auto& [unusedEntityId, binding] : m_bindings)
            {
                (void)unusedEntityId;
                if (binding.isStarted)
                {
                    continue;
                }

                const Result result =
                    m_module->on_create(binding.instanceHandle);
                if (!result)
                {
                    return result;
                }
                binding.isStarted = true;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::create_instance(
        GameCore::EntityId a_entityId,
        GameCore::Generation a_generation,
        const std::string& a_className) noexcept
    {
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }
        // Scene 上の className をそのまま生成に使わず、DLL の登録情報で先に検証する
        if (!m_module->has_class(a_className.c_str()))
        {
            return Result::fail(Code::NotFound, Severity::Error,
                                "Script class was not registered by the module.");
        }

        const ScriptClassInfo* classInfo =
            m_module->find_class_info(a_className);
        if (classInfo == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                                "Script class metadata was not found.");
        }

        ECS::ScriptComponent* script = nullptr;
        Result result =
            m_world.get_component<ECS::ScriptComponent>(a_entityId, script);
        if (!result || script == nullptr)
        {
            return result
                       ? Result::fail(Code::NotFound, Severity::Error,
                                      "ScriptComponent was not found.")
                       : result;
        }

        std::vector<Core::Native::ScriptFieldValue> fieldValues{};
        fieldValues.reserve(classInfo->fields.size());
        for (const ScriptFieldInfo& fieldInfo : classInfo->fields)
        {
            fieldValues.push_back(make_abi_field_value(
                fieldInfo, find_field_value(*script, fieldInfo.name)));
        }

        ScriptInstanceCreateInfo createInfo{};
        createInfo.entityId = a_entityId;
        createInfo.generation = a_generation;
        createInfo.className = a_className.c_str();
        createInfo.fieldValues = fieldValues.data();
        createInfo.fieldCount = static_cast<uint32_t>(fieldValues.size());

        ScriptInstanceHandle instanceHandle = k_invalidScriptInstanceHandle;
        result = m_module->create_instance(createInfo, instanceHandle);
        if (!result)
        {
            return result;
        }
        if (instanceHandle == k_invalidScriptInstanceHandle)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module created an invalid script instance handle.");
        }

        m_bindings.emplace(
            a_entityId,
            Binding{
                a_className, instanceHandle, a_generation, false});
        return Result::ok();
    }

    Result ScriptRuntime::destroy_instance(GameCore::EntityId a_entityId) noexcept
    {
        const auto binding = m_bindings.find(a_entityId);
        if (binding == m_bindings.end())
        {
            return Result::ok();
        }
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module is not loaded.");
        }

        const Result result = m_module->destroy_instance(binding->second.instanceHandle);
        if (!result)
        {
            return result;
        }

        m_bindings.erase(binding);
        return Result::ok();
    }

    Core::Native::ScriptAbiResult ScriptRuntime::script_is_entity_valid(
        void* a_userData,
        Core::Native::ScriptEntityHandle a_entity,
        uint8_t* a_outIsValid) noexcept
    {
        if (a_userData == nullptr || a_outIsValid == nullptr)
        {
            return Core::Native::ScriptAbiResult::InvalidArgument;
        }

        auto* runtime = static_cast<ScriptRuntime*>(a_userData);
        bool isAlive = false;
        const Result result = runtime->m_world.is_alive(
            a_entity.entityId, a_entity.generation, isAlive);
        if (!result)
        {
            return to_script_abi_result(result);
        }

        *a_outIsValid = isAlive ? 1u : 0u;
        return Core::Native::ScriptAbiResult::Ok;
    }

    Core::Native::ScriptAbiResult ScriptRuntime::script_read_transform(
        void* a_userData,
        Core::Native::ScriptEntityHandle a_entity,
        Core::Native::ScriptTransform* a_outTransform) noexcept
    {
        if (a_userData == nullptr || a_outTransform == nullptr)
        {
            return Core::Native::ScriptAbiResult::InvalidArgument;
        }

        auto* runtime = static_cast<ScriptRuntime*>(a_userData);
        ECS::TransformComponent* transform = nullptr;
        const Result result = runtime->get_transform_component(a_entity, transform);
        if (!result)
        {
            return to_script_abi_result(result);
        }

        const Math::float3 rotation = Math::quaternion_to_euler_xyz(transform->rotation);
        a_outTransform->position = {
            transform->position.x, transform->position.y, transform->position.z};
        a_outTransform->rotation = {rotation.x, rotation.y, rotation.z};
        a_outTransform->scale = {
            transform->scale.x, transform->scale.y, transform->scale.z};
        return Core::Native::ScriptAbiResult::Ok;
    }

    Core::Native::ScriptAbiResult ScriptRuntime::script_write_transform(
        void* a_userData,
        Core::Native::ScriptEntityHandle a_entity,
        const Core::Native::ScriptTransform* a_transform) noexcept
    {
        if (a_userData == nullptr || a_transform == nullptr)
        {
            return Core::Native::ScriptAbiResult::InvalidArgument;
        }

        const float values[] = {
            a_transform->position.x,
            a_transform->position.y,
            a_transform->position.z,
            a_transform->rotation.x,
            a_transform->rotation.y,
            a_transform->rotation.z,
            a_transform->scale.x,
            a_transform->scale.y,
            a_transform->scale.z};
        for (const float value : values)
        {
            if (!std::isfinite(value))
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }
        }

        auto* runtime = static_cast<ScriptRuntime*>(a_userData);
        ECS::TransformComponent* transform = nullptr;
        const Result result = runtime->get_transform_component(a_entity, transform);
        if (!result)
        {
            return to_script_abi_result(result);
        }

        // Script の Euler 表現は ABI 境界だけで使い、Runtime World では Quaternion を正規形とする
        transform->position = Math::float3(
            a_transform->position.x, a_transform->position.y, a_transform->position.z);
        transform->rotation = Math::quaternion_from_euler_xyz(Math::float3(
            a_transform->rotation.x, a_transform->rotation.y, a_transform->rotation.z));
        transform->scale = Math::float3(
            a_transform->scale.x, a_transform->scale.y, a_transform->scale.z);
        return Core::Native::ScriptAbiResult::Ok;
    }

    Core::Native::ScriptAbiResult ScriptRuntime::script_find_instance(
        void* a_userData,
        Core::Native::ScriptEntityHandle a_entity,
        Core::Native::ScriptStringView a_className,
        Core::Native::ScriptInstanceHandle* a_outHandle) noexcept
    {
        if (a_userData == nullptr || a_outHandle == nullptr ||
            a_className.data == nullptr || a_className.size == 0u)
        {
            return Core::Native::ScriptAbiResult::InvalidArgument;
        }

        *a_outHandle = Core::Native::k_invalidScriptInstanceHandle;
        auto* runtime = static_cast<ScriptRuntime*>(a_userData);
        const auto binding = runtime->m_bindings.find(a_entity.entityId);
        if (binding == runtime->m_bindings.end() ||
            binding->second.generation != a_entity.generation ||
            binding->second.className !=
                std::string_view(a_className.data, a_className.size))
        {
            return Core::Native::ScriptAbiResult::NotFound;
        }

        a_outHandle->value = binding->second.instanceHandle;
        return Core::Native::ScriptAbiResult::Ok;
    }

    Core::Native::ScriptAbiResult ScriptRuntime::script_is_instance_valid(
        void* a_userData,
        Core::Native::ScriptInstanceHandle a_handle,
        uint8_t* a_outIsValid) noexcept
    {
        if (a_userData == nullptr || a_outIsValid == nullptr)
        {
            return Core::Native::ScriptAbiResult::InvalidArgument;
        }

        auto* runtime = static_cast<ScriptRuntime*>(a_userData);
        const auto binding = std::find_if(
            runtime->m_bindings.begin(), runtime->m_bindings.end(),
            [a_handle](const auto& a_value)
            {
                return a_value.second.instanceHandle == a_handle.value;
            });
        *a_outIsValid = binding != runtime->m_bindings.end() ? 1u : 0u;
        return Core::Native::ScriptAbiResult::Ok;
    }

    Core::Native::ScriptAbiResult ScriptRuntime::script_invoke_function(
        void* a_userData,
        Core::Native::ScriptInstanceHandle a_handle,
        Core::Native::ScriptStringView a_functionName) noexcept
    {
        if (a_userData == nullptr || a_functionName.data == nullptr ||
            a_functionName.size == 0u)
        {
            return Core::Native::ScriptAbiResult::InvalidArgument;
        }

        auto* runtime = static_cast<ScriptRuntime*>(a_userData);
        if (runtime->m_module == nullptr)
        {
            return Core::Native::ScriptAbiResult::InvalidState;
        }

        const Result result = runtime->m_module->invoke(
            a_handle.value,
            std::string_view(a_functionName.data, a_functionName.size));
        return to_script_abi_result(result);
    }

    Core::Native::ScriptAbiResult ScriptRuntime::to_script_abi_result(
        const Result& a_result) noexcept
    {
        switch (a_result.code)
        {
        case Code::OK:
            return Core::Native::ScriptAbiResult::Ok;
        case Code::InvalidArgument:
            return Core::Native::ScriptAbiResult::InvalidArgument;
        case Code::NotFound:
            return Core::Native::ScriptAbiResult::NotFound;
        case Code::InvalidState:
            return Core::Native::ScriptAbiResult::InvalidState;
        default:
            return Core::Native::ScriptAbiResult::InternalError;
        }
    }

    Result ScriptRuntime::get_transform_component(
        Core::Native::ScriptEntityHandle a_entity,
        ECS::TransformComponent*& a_outTransform) noexcept
    {
        bool isAlive = false;
        Result result = m_world.is_alive(a_entity.entityId, a_entity.generation, isAlive);
        if (!result)
        {
            return result;
        }
        if (!isAlive)
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "Script entity is no longer alive.");
        }

        result = m_world.get_component<ECS::TransformComponent>(a_entity.entityId, a_outTransform);
        if (!result)
        {
            return result;
        }
        if (a_outTransform == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "Script entity has no TransformComponent.");
        }

        return Result::ok();
    }

} // namespace Cue::Script
