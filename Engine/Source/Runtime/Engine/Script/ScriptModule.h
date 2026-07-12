#pragma once

/// **********************************************************************
/// GameScript DLL のロードと ABI 呼び出しを管理する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/Path.h>

// === Engine includes ===
#include "ScriptModuleApi.h"

namespace Cue::Script
{
    /// @brief GameScript DLL の所有と ABI 呼び出しを管理
    ///
    /// DLL 解放時に exports 内の関数ポインタも無効になるため、instance を保持する
    /// ScriptRuntime より先の unload を禁止
    class ScriptModule final
    {
    public:
        ScriptModule() = default;
        ~ScriptModule();

        ScriptModule(const ScriptModule&) = delete;
        ScriptModule& operator=(const ScriptModule&) = delete;
        ScriptModule(ScriptModule&&) = delete;
        ScriptModule& operator=(ScriptModule&&) = delete;

        /// @brief 指定 DLL の ABI と exports を検証してロード
        ///
        /// 読み込みに失敗した DLL の関数ポインタを runtime が参照しないよう、成功時だけ利用可能状態へ遷移
        [[nodiscard]] Result load(const Core::IO::Path& a_modulePath) noexcept;

        /// @brief ロード済み DLL と exports を無効化
        ///
        /// Script instance の破棄後にだけ呼び出し、DLL 内の object を解放できない状態を防止
        void unload() noexcept;

        /// @brief 必要な ABI exports を呼び出せる状態を返却
        [[nodiscard]] bool is_loaded() const noexcept;

        /// @brief Scene に記録された className が module 内で解決できるかを返却
        [[nodiscard]] bool has_class(const char* a_className) const noexcept;

        /// @brief DLL 側に Script instance の生成を要求
        ///
        /// instance の実体を Engine 側へ公開せず、DLL をまたぐ C++ 所有権を発生させません
        [[nodiscard]] Result create_instance(
            const ScriptInstanceCreateInfo& a_createInfo,
            ScriptInstanceHandle& a_outHandle) const noexcept;

        /// @brief DLL 側が所有する Script instance を破棄
        [[nodiscard]] Result destroy_instance(ScriptInstanceHandle a_handle) const noexcept;

        /// @brief 生成済み instance の OnCreate lifecycle を呼び出し
        [[nodiscard]] Result on_create(ScriptInstanceHandle a_handle) const noexcept;

        /// @brief 生成済み instance の OnUpdate lifecycle を呼び出し
        [[nodiscard]] Result on_update(
            ScriptInstanceHandle a_handle,
            float a_deltaTimeSeconds) const noexcept;

    private:
        [[nodiscard]] static Result convert_result(ScriptResult a_result) noexcept;

        void* m_nativeHandle = nullptr; // DLL を解放するまで exports の呼び出し先を有効に保つ所有 handle
        Core::IO::Path m_modulePath{};  // エラー調査時にロード元を特定できるよう正規化して保持する
        ScriptModuleExports m_exports{}; // ABI 検証済みの関数だけを runtime に公開する
    };
} // namespace Cue::Script
