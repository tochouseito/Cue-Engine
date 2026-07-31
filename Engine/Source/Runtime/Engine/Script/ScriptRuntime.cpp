#include "ScriptRuntime.h"

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameWorld.h"
#include "ScriptModule.h"

// === C++ includes ===
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Cue::Script
{
    ScriptRuntime::ScriptRuntime(GameCore::GameWorld& a_world) noexcept
        : m_world(a_world)
    {
        // DLL callback は Runtime ごとに異なる World を参照するため、global state を介さず userData に自身を保持する
        m_scriptEngineApi.structSize = sizeof(Core::Native::ScriptEngineApi);
        m_scriptEngineApi.userData = this;
        m_scriptEngineApi.isEntityValid = &ScriptRuntime::script_is_entity_valid;
        m_scriptEngineApi.readTransform = &ScriptRuntime::script_read_transform;
        m_scriptEngineApi.writeTransform = &ScriptRuntime::script_write_transform;
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

        return sync_instances();
    }

    Result ScriptRuntime::update(float a_deltaTimeSeconds) noexcept
    {
        Result result = sync_instances();
        if (!result)
        {
            return result;
        }

        for (auto& [unusedEntityId, binding] : m_bindings)
        {
            (void)unusedEntityId;
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

    const Core::Native::ScriptEngineApi& ScriptRuntime::script_engine_api() const noexcept
    {
        return m_scriptEngineApi;
    }

    Result ScriptRuntime::sync_instances() noexcept
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

            const Result result = create_instance(entityId, desired.generation, desired.className);
            if (!result)
            {
                return result;
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

        ScriptInstanceCreateInfo createInfo{};
        createInfo.entityId = a_entityId;
        createInfo.generation = a_generation;
        createInfo.className = a_className.c_str();

        ScriptInstanceHandle instanceHandle = k_invalidScriptInstanceHandle;
        Result result = m_module->create_instance(createInfo, instanceHandle);
        if (!result)
        {
            return result;
        }
        if (instanceHandle == k_invalidScriptInstanceHandle)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module created an invalid script instance handle.");
        }

        // OnCreate 失敗時に handle を残さず、DLL 側の on_destroy も同じ破棄経路で実行する
        result = m_module->on_create(instanceHandle);
        if (!result)
        {
            (void)m_module->destroy_instance(instanceHandle);
            return result;
        }

        m_bindings.emplace(a_entityId, Binding{a_className, instanceHandle, a_generation});
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
