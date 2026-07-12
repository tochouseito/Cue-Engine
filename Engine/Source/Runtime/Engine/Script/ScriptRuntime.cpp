#include "ScriptRuntime.h"

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameWorld.h"
#include "ScriptModule.h"

// === C++ includes ===
#include <unordered_map>
#include <utility>

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

        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::ok();
        }

        for (const auto& [entityId, binding] : m_bindings)
        {
            (void)entityId;
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
        if (m_bindings.empty())
        {
            return Result::ok();
        }
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script module was unloaded while script instances are still active.");
        }

        for (const auto& [entityId, binding] : m_bindings)
        {
            (void)entityId;
            const Result result = m_module->destroy_instance(binding.instanceHandle);
            if (!result)
            {
                return result;
            }
        }

        m_bindings.clear();
        return Result::ok();
    }

    Result ScriptRuntime::sync_instances() noexcept
    {
        // World を走査中に bindings を変更しないよう、先に現在必要な class の snapshot を作る
        std::unordered_map<GameCore::EntityId, std::string> desiredClasses{};
        Result collectionResult = Result::ok();
        const Result enumerateResult = m_world.for_each_object(
            [&](GameCore::EntityId a_entityId, GameCore::GameObject)
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
                    desiredClasses.emplace(a_entityId, script->className);
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
            const auto desired = desiredClasses.find(iterator->first);
            if (desired != desiredClasses.end() && desired->second == iterator->second.className)
            {
                ++iterator;
                continue;
            }

            const Result result = m_module != nullptr && m_module->is_loaded()
                                      ? m_module->destroy_instance(iterator->second.instanceHandle)
                                      : Result::fail(Code::InvalidState, Severity::Error,
                                                     "Script module was unloaded while script instances are still active.");
            if (!result)
            {
                return result;
            }
            iterator = m_bindings.erase(iterator);
        }

        if (desiredClasses.empty())
        {
            return Result::ok();
        }
        // ScriptComponent がある場合だけ module を必須にし、Script を使わない Scene は DLL なしで Play できる
        if (m_module == nullptr || !m_module->is_loaded())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "Script components require a loaded script module.");
        }

        for (const auto& [entityId, className] : desiredClasses)
        {
            if (m_bindings.contains(entityId))
            {
                continue;
            }

            const Result result = create_instance(entityId, className);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result ScriptRuntime::create_instance(
        GameCore::EntityId a_entityId,
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

        // OnCreate 失敗時に handle を bindings へ残さず、DLL 側の object も同じ失敗経路で回収する
        result = m_module->on_create(instanceHandle);
        if (!result)
        {
            (void)m_module->destroy_instance(instanceHandle);
            return result;
        }

        m_bindings.emplace(a_entityId, Binding{a_className, instanceHandle});
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
} // namespace Cue::Script
