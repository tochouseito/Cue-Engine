#pragma once

/// **********************************************************************
/// GameScript DLL 内で Script instance を所有する最小 runtime を定義する
/// **********************************************************************

// === Native includes ===
#include "ScriptAbi.h"

// === C++ includes ===
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Cue::Core::Native
{
    /// @brief DLL が提供する Script class の生成規則
    struct ScriptClassDefinition final
    {
        ScriptStringView className{};
        ScriptCreateStateFn createState = nullptr;
        ScriptDestroyStateFn destroyState = nullptr;
        ScriptStartStateFn startState = nullptr;
        ScriptUpdateStateFn updateState = nullptr;
    };

    /// @brief GameScript DLL が所有する Script instance の管理器
    ///
    /// Engine は不透明 handle のみを保持し、DLL 内 object の所有権を越境させない
    class ScriptModuleRuntime final
    {
      public:
        ScriptModuleRuntime() = default;
        ~ScriptModuleRuntime();

        ScriptModuleRuntime(const ScriptModuleRuntime&) = delete;
        ScriptModuleRuntime& operator=(const ScriptModuleRuntime&) = delete;
        ScriptModuleRuntime(ScriptModuleRuntime&&) = delete;
        ScriptModuleRuntime& operator=(ScriptModuleRuntime&&) = delete;

        /// @brief Engine API と class 定義を検証して Script を利用可能にする
        [[nodiscard]] ScriptAbiResult
        register_scripts(const ScriptEngineApi* a_engineApi,
                         const ScriptClassDefinition* a_classDefinitions,
                         uint32_t a_classCount) noexcept;

        /// @brief 指定 class の DLL 所有 instance を生成する
        [[nodiscard]] ScriptAbiResult
        create_instance(const ScriptCreateInfo* a_createInfo,
                        ScriptInstanceHandle* a_outInstanceHandle) noexcept;

        /// @brief 指定 instance を破棄する
        [[nodiscard]] ScriptAbiResult
        destroy_instance(ScriptInstanceHandle a_instanceHandle) noexcept;

        /// @brief 指定 instance の初期化を DLL 側へ委譲する
        [[nodiscard]] ScriptAbiResult
        start_instance(ScriptInstanceHandle a_instanceHandle) noexcept;

        /// @brief 指定 instance の更新を DLL 側へ委譲する
        [[nodiscard]] ScriptAbiResult
        update_instance(ScriptInstanceHandle a_instanceHandle,
                        float a_deltaTimeSeconds) noexcept;

      private:
        struct Instance final
        {
            const ScriptClassDefinition* definition = nullptr;
            void* state = nullptr;
        };

        [[nodiscard]] static bool
        is_valid_string_view(ScriptStringView a_value) noexcept;
        [[nodiscard]] static bool
        is_valid_engine_api(const ScriptEngineApi* a_engineApi) noexcept;
        [[nodiscard]] static bool
        is_valid_class_definition(const ScriptClassDefinition& a_definition) noexcept;
        void destroy_all_instances() noexcept;

        const ScriptEngineApi* m_engineApi =
            nullptr; // DLL が利用中だけ有効な Engine 側の非所有 API
        std::unordered_map<std::string, const ScriptClassDefinition*>
            m_classDefinitions{}; // class 名から factory を解決する索引
        std::unordered_map<uint64_t, Instance>
            m_instances{}; // DLL が所有する state を不透明 handle で管理する
        uint64_t m_nextInstanceValue = 1u;
    };
} // namespace Cue::Core::Native
