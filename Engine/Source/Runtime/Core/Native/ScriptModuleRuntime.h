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
    /// @brief 公開 field の metadata と Script object への反映処理
    struct ScriptFieldDefinition final
    {
        ScriptFieldValue defaultValue{};
        ScriptApplyFieldFn applyValue = nullptr;
    };

    /// @brief 別 Script から名前で呼び出せる引数なし関数
    struct ScriptFunctionDefinition final
    {
        ScriptStringView name{};
        ScriptInvokeStateFn invokeState = nullptr;
    };

    /// @brief DLL が提供する Script class の生成規則
    struct ScriptClassDefinition final
    {
        ScriptStringView className{};
        ScriptCreateStateFn createState = nullptr;
        ScriptDestroyStateFn destroyState = nullptr;
        ScriptStartStateFn startState = nullptr;
        ScriptUpdateStateFn updateState = nullptr;
        const ScriptFieldDefinition* fields = nullptr;
        uint32_t fieldCount = 0u;
        const ScriptFunctionDefinition* functions = nullptr;
        uint32_t functionCount = 0u;
        ScriptStateDescriptor stateDescriptor{};
        ScriptGetStateSizeFn getStateSize = nullptr;
        ScriptSerializeStateFn serializeState = nullptr;
        ScriptRestoreStateFn restoreState = nullptr;
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

        /// @brief instance の登録済み引数なし関数を名前で呼び出す
        [[nodiscard]] ScriptAbiResult
        invoke_instance(ScriptInstanceHandle a_instanceHandle,
                        ScriptStringView a_functionName) noexcept;

        /// @brief class の reload state 互換情報を返す
        [[nodiscard]] ScriptAbiResult
        get_state_descriptor(ScriptStringView a_className,
                             ScriptStateDescriptor* a_outDescriptor) const noexcept;

        /// @brief instance の reload state 保存領域サイズを返す
        [[nodiscard]] ScriptAbiResult
        get_instance_state_size(ScriptInstanceHandle a_instanceHandle,
                                uint32_t* a_outStateSize) const noexcept;

        /// @brief instance の reload state を byte 列へ保存する
        [[nodiscard]] ScriptAbiResult
        serialize_instance(ScriptInstanceHandle a_instanceHandle,
                           void* a_outStateBuffer,
                           uint32_t a_stateBufferSize) const noexcept;

        /// @brief byte 列から instance の reload state を復元する
        [[nodiscard]] ScriptAbiResult
        restore_instance(ScriptInstanceHandle a_instanceHandle,
                         const void* a_stateBuffer,
                         uint32_t a_stateBufferSize) noexcept;

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
        [[nodiscard]] const ScriptClassDefinition*
        find_class_definition(ScriptStringView a_className) const noexcept;
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
