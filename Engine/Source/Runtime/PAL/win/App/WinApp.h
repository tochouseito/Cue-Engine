#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <cstdint>
#include <functional>
#include <memory>

// === Windows API include ===
#include "win_platform.h"

namespace Cue::PAL::Win
{
    enum class ShowWindowFlag : uint8_t
    {
        Normal = 0,
        Maximized,
    };

    // Windows アプリケーション
    class WinApp final
    {
    public:
        // メッセージハンドラ
        using MessageHandler = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)>;
        // メッセージハンドラエントリ
        struct MessageHandlerEntry
        {
            uint64_t m_id = 0;
            MessageHandler m_handler;
        };
        // ウィンドウサイズ
        struct WindowSize
        {
            uint32_t width = 0;
            uint32_t height = 0;
        };

        WinApp();
        ~WinApp();

        // --- ウィンドウ操作 ---
        Result create_window(uint32_t width, uint32_t height, const wchar_t* className, const wchar_t* titleName);
        void destroy_window();
        void show_window(ShowWindowFlag flag = ShowWindowFlag::Normal);
        PlatformMessage pump_message();

        // --- サイズ取得 ---
        [[nodiscard]] WindowSize get_client_size() const noexcept;
        [[nodiscard]] WindowSize get_window_size() const noexcept;

        // --- メッセージハンドラ管理 ---
        LRESULT on_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    private:
        HWND m_hwnd = nullptr; // ウィンドウハンドル
        bool m_shouldClose = false; // 終了フラグ
        uint64_t m_nextMessageHandlerId = 1; // 次のメッセージハンドラ ID
        std::vector<MessageHandlerEntry> m_messageHandlers; // メッセージハンドラエントリのリスト
    };
}
