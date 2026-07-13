#include "ScriptRuntime.h"

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameWorld.h"
#include "ScriptModule.h"

// === C++ includes ===
#include <unordered_map>
#include <utility>
#include <vector>

namespace Cue::Script
{
    ScriptRuntime::ScriptRuntime(GameCore::GameWorld& a_world) noexcept
        : m_world(a_world)
    {
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
        return sync_instances();
    }

    Result ScriptRuntime::update(float a_deltaTimeSeconds) noexcept
    {
        Result result = sync_instances();
        if (!result)
        {
            return result;
        }

        for (auto& [entityId, binding] : m_bindings)
        {
            const auto component = m_marionnetteComponents.find(entityId);
            if (component == m_marionnetteComponents.end())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "Script instance has no Marionnette component.");
            }

            if (!binding.hasStarted)
            {
                component->second->start();
                binding.hasStarted = true;
            }

            component->second->update(a_deltaTimeSeconds);
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

        result = bind_marionnette(a_entityId, a_generation);
        if (!result)
        {
            (void)m_module->destroy_instance(instanceHandle);
            return result;
        }

        result = bind_marionnette_component(a_entityId, a_generation);
        if (!result)
        {
            unbind_marionnette(a_entityId);
            (void)m_module->destroy_instance(instanceHandle);
            return result;
        }

        const auto component = m_marionnetteComponents.find(a_entityId);
        if (component == m_marionnetteComponents.end())
        {
            unbind_marionnette(a_entityId);
            (void)m_module->destroy_instance(instanceHandle);
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script Marionnette component was not bound.");
        }

        component->second->awake();

        // OnCreate 失敗時に handle と runtime 側の owner を残さず、同じ失敗経路で回収する
        result = m_module->on_create(instanceHandle);
        if (!result)
        {
            component->second->on_destroy();
            unbind_marionnette_component(a_entityId);
            unbind_marionnette(a_entityId);
            (void)m_module->destroy_instance(instanceHandle);
            return result;
        }

        m_bindings.emplace(a_entityId, Binding{a_className, instanceHandle, a_generation, false});
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

        const auto component = m_marionnetteComponents.find(a_entityId);
        if (component != m_marionnetteComponents.end())
        {
            component->second->on_destroy();
        }

        const Result result = m_module->destroy_instance(binding->second.instanceHandle);
        if (!result)
        {
            return result;
        }

        m_bindings.erase(binding);
        unbind_marionnette_component(a_entityId);
        unbind_marionnette(a_entityId);
        return Result::ok();
    }

    Result ScriptRuntime::bind_marionnette(
        GameCore::EntityId a_entityId,
        GameCore::Generation a_generation) noexcept
    {
        auto marionnette = std::make_unique<Marionnette>();
        marionnette->bind(this, &m_world, a_entityId, a_generation);
        if (!marionnette->is_valid())
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "Script Marionnette target was not found.");
        }

        m_marionnettes.emplace(a_entityId, std::move(marionnette));
        return Result::ok();
    }

    Result ScriptRuntime::bind_marionnette_component(
        GameCore::EntityId a_entityId,
        GameCore::Generation a_generation) noexcept
    {
        const auto owner = m_marionnettes.find(a_entityId);
        if (owner == m_marionnettes.end())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script Marionnette owner is not bound.");
        }

        auto component = std::make_unique<MarionnetteComponent>();
        component->bind(this, &m_world, a_entityId, a_generation, owner->second.get());
        m_marionnetteComponents.emplace(a_entityId, std::move(component));
        return Result::ok();
    }

    void ScriptRuntime::unbind_marionnette_component(GameCore::EntityId a_entityId) noexcept
    {
        const auto component = m_marionnetteComponents.find(a_entityId);
        if (component == m_marionnetteComponents.end())
        {
            return;
        }

        component->second->unbind();
        m_marionnetteComponents.erase(component);
    }

    void ScriptRuntime::unbind_marionnette(GameCore::EntityId a_entityId) noexcept
    {
        const auto marionnette = m_marionnettes.find(a_entityId);
        if (marionnette == m_marionnettes.end())
        {
            return;
        }

        marionnette->second->unbind();
        m_marionnettes.erase(marionnette);
    }

} // namespace Cue::Script
