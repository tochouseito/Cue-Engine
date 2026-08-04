#pragma once

/// **********************************************************************
/// GameScript DLL が公開する Script class 定義を収集する
/// **********************************************************************

// === Script includes ===
#include "MarionnetteBehaviour.h"

// === C++ includes ===
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Cue::Script
{
    /// @brief GameScript DLL 内で Script class 定義を一意に保持する
    class ScriptClassRegistry final
    {
    public:
        /// @brief DLL ごとの Script class registry を返す
        [[nodiscard]] static ScriptClassRegistry& instance() noexcept;

        /// @brief Script class 定義を登録する
        ///
        /// 静的初期化中でも同名 class を曖昧に解決しないため、無効な登録を検出した DLL は利用不可にする
        void register_class(Core::Native::ScriptClassDefinition a_definition);

        /// @brief 登録済みの全 class 定義が利用可能かを返す
        ///
        /// 同名登録を検出した DLL を Engine が load 前に拒否できるようにする
        [[nodiscard]] bool is_valid() const noexcept;

        /// @brief 指定 class が登録済みかを返す
        [[nodiscard]] bool has_class(std::string_view a_className) const noexcept;

        /// @brief 指定 class の field/function/state 定義を返す
        [[nodiscard]] const Core::Native::ScriptClassDefinition*
        find_class_definition(std::string_view a_className) const noexcept;

        /// @brief Engine へ公開する Script class 数を返す
        [[nodiscard]] uint32_t class_count() const noexcept;

        /// @brief 指定位置の Script class 名を返す
        ///
        /// 返却文字列は DLL がロードされている間だけ有効であり、Engine は ABI 境界で複製する
        [[nodiscard]] const char* class_name(uint32_t a_index) const noexcept;

        /// @brief DLL 内の Script instance 生成に使う class 定義群を返す
        [[nodiscard]] std::span<const Core::Native::ScriptClassDefinition>
        class_definitions() const noexcept;

    private:
        std::vector<Core::Native::ScriptClassDefinition> m_classDefinitions{};
        bool m_isValid = true;
    };

    /// @brief Script translation unit の class 登録を DLL 初期化時に実行する
    class ScriptClassRegistration final
    {
    public:
        explicit ScriptClassRegistration(Core::Native::ScriptClassDefinition a_definition);
    };
} // namespace Cue::Script

#define CUE_SCRIPT_CONCATENATE_INTERNAL(a_left, a_right) a_left##a_right
#define CUE_SCRIPT_CONCATENATE(a_left, a_right) CUE_SCRIPT_CONCATENATE_INTERNAL(a_left, a_right)

// Script source ごとの静的登録にし、GameScriptModule.cpp を Script 追加のたびに編集させない
#define CUE_REGISTER_SCRIPT_CLASS(a_className, a_scriptType) \
    namespace \
    { \
        const ::Cue::Script::ScriptClassRegistration \
            CUE_SCRIPT_CONCATENATE(g_scriptClassRegistration, __COUNTER__){ \
                ::Cue::Script::make_marionnette_script_class_definition<a_scriptType>(a_className)}; \
    }
