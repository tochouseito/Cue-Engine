#pragma once

/// ********************************************************************************
/// windows アプリケーション
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PlatformMessage.h>

// === win_platform includes ===
#include "stdafx.h"

// === C++ includes ===
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Cue::PAL::Win
{
    /// @brief ウィンドウ表示方法
    enum class ShowWindowFlag : uint8_t
    {
        Normal = 0,
        Maximized,
    };

    /// @brief Windows アプリケーションクラス
    class WinApp final
    {
    public:
        struct WindowSize
        {
            uint32_t width = 0;
            uint32_t height = 0;
        };

        WinApp();
        ~WinApp();

        /// @brief Win32 から呼ばれる静的ウィンドウプロシージャ
        static LRESULT CALLBACK window_proc(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);

        /// @brief ウィンドウメッセージを処理
        LRESULT on_message(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);

        // --- ウィンドウ操作 ---
        /// @brief ウィンドウを作成
        Result create_window(uint32_t a_width, uint32_t a_height, const wchar_t* a_className, const wchar_t* a_titleName);
        /// @brief ウィンドウを破棄
        void destroy_window();
        /// @brief ウィンドウを表示
        Result show_window(ShowWindowFlag a_flag = ShowWindowFlag::Normal);
        /// @brief キューから Windows メッセージを処理
        PlatformMessage pump_message();

        // --- サイズ取得 ---
        /// @brief クライアント領域サイズ
        [[nodiscard]] WindowSize get_client_size() const noexcept;
        /// @brief ウィンドウ全体サイズ
        [[nodiscard]] WindowSize get_window_size() const noexcept;

        /// @brief 管理中のウィンドウハンドル
        [[nodiscard]] HWND get_window_handle() const noexcept
        {
            return m_hwnd;
        }

        /// @brief コマンドブリッジをセット
        /// @param a_bridge コマンドブリッジ
        void set_command_bridge(Core::CQRS::Bridge* a_bridge) noexcept
        {
            m_commandBridge = a_bridge;
        }
    private:
        HWND m_hwnd = nullptr; // ウィンドウハンドル
        bool m_shouldClose = false; // 終了フラグ
        Core::CQRS::Bridge* m_commandBridge = nullptr; // コマンドブリッジ
    };
}
