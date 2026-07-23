#include "ScriptModuleRuntime.h"

// === C++ includes ===
#include <utility>

namespace Cue::Core::Native
{
    ScriptModuleRuntime::~ScriptModuleRuntime()
    {
        // Engine からの明示破棄に失敗しても、DLL unload 時に Script object の破棄を残さない
        destroy_all_instances();
    }

    ScriptAbiResult ScriptModuleRuntime::register_scripts(
        const ScriptEngineApi* a_engineApi,
        const ScriptClassDefinition* a_classDefinitions,
        uint32_t a_classCount) noexcept
    {
        if (!is_valid_engine_api(a_engineApi) ||
            (a_classCount > 0u && a_classDefinitions == nullptr))
        {
            return ScriptAbiResult::InvalidArgument;
        }
        if (!m_instances.empty())
        {
            // 既存 instance が古い class
            // 定義を参照したままになるため、実行中の再登録を拒否する
            return ScriptAbiResult::InvalidState;
        }

        std::unordered_map<std::string, const ScriptClassDefinition*> definitions{};
        for (uint32_t index = 0u; index < a_classCount; ++index)
        {
            const ScriptClassDefinition& definition = a_classDefinitions[index];
            if (!is_valid_class_definition(definition))
            {
                return ScriptAbiResult::InvalidArgument;
            }

            const std::string className(definition.className.data,
                                        definition.className.size);
            const auto [iterator, wasInserted] =
                definitions.emplace(className, &definition);
            (void)iterator;
            if (!wasInserted)
            {
                // 同名 class は Scene の className 解決を曖昧にするため、DLL
                // 登録時に止める
                return ScriptAbiResult::InvalidArgument;
            }
        }

        m_engineApi = a_engineApi;
        m_classDefinitions = std::move(definitions);
        return ScriptAbiResult::Ok;
    }

    ScriptAbiResult ScriptModuleRuntime::create_instance(
        const ScriptCreateInfo* a_createInfo,
        ScriptInstanceHandle* a_outInstanceHandle) noexcept
    {
        if (a_createInfo == nullptr || a_outInstanceHandle == nullptr ||
            !is_valid_string_view(a_createInfo->className) ||
            m_engineApi == nullptr)
        {
            return ScriptAbiResult::InvalidArgument;
        }

        const std::string className(a_createInfo->className.data,
                                    a_createInfo->className.size);
        const auto definition = m_classDefinitions.find(className);
        if (definition == m_classDefinitions.end())
        {
            return ScriptAbiResult::NotFound;
        }

        void* state = nullptr;
        const ScriptAbiResult createResult =
            definition->second->createState(m_engineApi, a_createInfo, &state);
        if (createResult != ScriptAbiResult::Ok)
        {
            if (state != nullptr)
            {
                definition->second->destroyState(state);
            }
            return createResult;
        }
        if (state == nullptr)
        {
            return ScriptAbiResult::InvalidState;
        }

        uint64_t instanceValue = m_nextInstanceValue++;
        if (instanceValue == k_invalidScriptInstanceHandle.value)
        {
            instanceValue = m_nextInstanceValue++;
        }

        const auto [iterator, wasInserted] =
            m_instances.emplace(instanceValue, Instance{definition->second, state});
        (void)iterator;
        if (!wasInserted)
        {
            definition->second->destroyState(state);
            return ScriptAbiResult::InternalError;
        }

        a_outInstanceHandle->value = instanceValue;
        return ScriptAbiResult::Ok;
    }

    ScriptAbiResult ScriptModuleRuntime::destroy_instance(
        ScriptInstanceHandle a_instanceHandle) noexcept
    {
        if (a_instanceHandle.value == k_invalidScriptInstanceHandle.value)
        {
            return ScriptAbiResult::InvalidArgument;
        }

        const auto instance = m_instances.find(a_instanceHandle.value);
        if (instance == m_instances.end())
        {
            return ScriptAbiResult::NotFound;
        }

        instance->second.definition->destroyState(instance->second.state);
        m_instances.erase(instance);
        return ScriptAbiResult::Ok;
    }

    ScriptAbiResult ScriptModuleRuntime::start_instance(
        ScriptInstanceHandle a_instanceHandle) noexcept
    {
        if (a_instanceHandle.value == k_invalidScriptInstanceHandle.value)
        {
            return ScriptAbiResult::InvalidArgument;
        }

        const auto instance = m_instances.find(a_instanceHandle.value);
        if (instance == m_instances.end())
        {
            return ScriptAbiResult::NotFound;
        }

        return instance->second.definition->startState(instance->second.state);
    }

    ScriptAbiResult
    ScriptModuleRuntime::update_instance(ScriptInstanceHandle a_instanceHandle,
                                         float a_deltaTimeSeconds) noexcept
    {
        if (a_instanceHandle.value == k_invalidScriptInstanceHandle.value)
        {
            return ScriptAbiResult::InvalidArgument;
        }

        const auto instance = m_instances.find(a_instanceHandle.value);
        if (instance == m_instances.end())
        {
            return ScriptAbiResult::NotFound;
        }

        return instance->second.definition->updateState(instance->second.state,
                                                        a_deltaTimeSeconds);
    }

    bool ScriptModuleRuntime::is_valid_string_view(
        ScriptStringView a_value) noexcept
    {
        return a_value.data != nullptr && a_value.size > 0u;
    }

    bool ScriptModuleRuntime::is_valid_engine_api(
        const ScriptEngineApi* a_engineApi) noexcept
    {
        return a_engineApi != nullptr &&
               a_engineApi->structSize >= sizeof(ScriptEngineApi) &&
               a_engineApi->abiVersion == k_scriptAbiVersion &&
               a_engineApi->userData != nullptr &&
               a_engineApi->isEntityValid != nullptr &&
               a_engineApi->getTransformQuaternion != nullptr &&
               a_engineApi->setTransformQuaternion != nullptr &&
               a_engineApi->getTransformEuler != nullptr &&
               a_engineApi->setTransformEuler != nullptr &&
               a_engineApi->rotateTransformEuler != nullptr;
    }

    bool ScriptModuleRuntime::is_valid_class_definition(
        const ScriptClassDefinition& a_definition) noexcept
    {
        return is_valid_string_view(a_definition.className) &&
               a_definition.createState != nullptr &&
               a_definition.destroyState != nullptr &&
               a_definition.startState != nullptr &&
               a_definition.updateState != nullptr;
    }

    void ScriptModuleRuntime::destroy_all_instances() noexcept
    {
        for (const auto& [instanceHandle, instance] : m_instances)
        {
            (void)instanceHandle;
            if (instance.definition != nullptr && instance.definition->destroyState != nullptr &&
                instance.state != nullptr)
            {
                instance.definition->destroyState(instance.state);
            }
        }
        m_instances.clear();
    }
} // namespace Cue::Core::Native
