// WinApp の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// === Windows API includes ===
#include "PlatformMessage.h"
#include "stdafx.h"

namespace Cue::Core::CQRS
{
    class Bridge;
}

namespace Cue::PAL::Win
{
    /// @brief ウィンドウ表示方法
    enum class ShowWindowFlag : uint8_t
    {
        Normal = 0,
        Maximized,
    };

    /// @brief Windows ウィンドウの生成とメッセージ処理を担当し
    class WinApp final
    {
    public:
        using messageHandler = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)>;

        struct MessageHandlerEntry
        {
            uint64_t id = 0;
            messageHandler handler;
        };

        struct WindowSize
        {
            uint32_t width = 0;
            uint32_t height = 0;
        };

        /// @brief アプリケーション管理オブジェクトを構築する
        WinApp();
        /// @brief 管理中のウィンドウ資源を破棄する
        ~WinApp();

        // --- ウィンドウ操作 ---
        /// @brief ウィンドウを作成する
        Result create_window(uint32_t a_width, uint32_t a_height, const wchar_t* a_className, const wchar_t* a_titleName);
        /// @brief ウィンドウを破棄する
        void destroy_window();
        /// @brief ウィンドウを表示し
        Result show_window(ShowWindowFlag a_flag = ShowWindowFlag::Normal);
        /// @brief キューから Windows メッセージを処理し
        PlatformMessage pump_message();

        // --- サイズ取得 ---
        /// @brief クライアント領域サイズを返す
        [[nodiscard]] WindowSize get_client_size() const noexcept;
        /// @brief ウィンドウ全体サイズを返す
        [[nodiscard]] WindowSize get_window_size() const noexcept;

        /// @brief 管理中のウィンドウハンドルを返す
        [[nodiscard]] HWND get_window_handle() const noexcept
        {
            return m_hwnd;
        }

        /// @brief 管理中のウィンドウが foreground window か返す
        [[nodiscard]] bool is_window_focused() const noexcept;
        /// @brief ファイルのドラッグアンドドロップ受け付けを切り替え
        Result set_drag_drop_enabled(bool a_isEnabled);
        /// @brief ファイルのドラッグアンドドロップ受け付けが有効か返す
        [[nodiscard]] bool is_drag_drop_enabled() const noexcept
        {
            return m_isDragDropEnabled;
        }
        /// @brief ドロップされたファイルパスを取り出し
        [[nodiscard]] bool consume_dropped_files(
            std::vector<std::string>& a_outPaths) noexcept;

        /// @brief Platform 用 command bridge を設定する
        void set_platform_bridge(Core::CQRS::Bridge* a_bridge) noexcept
        {
            m_platformBridge = a_bridge;
        }

        // --- メッセージハンドラ管理 ---
        /// @brief メッセージハンドラ登録
        [[nodiscard]] uint64_t register_message_handler(messageHandler a_handler);
        /// @brief メッセージハンドラ解除
        bool unregister_message_handler(uint64_t handlerId);
        /// @brief ウィンドウメッセージを処理し
        LRESULT on_message(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);
        /// @brief Win32 から呼ばれる静的ウィンドウプロシージャ
        static LRESULT CALLBACK window_proc(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);

    private:
        HWND m_hwnd = nullptr; // ウィンドウハンドル
        bool m_shouldClose = false; // 終了フラグ
        bool m_isDragDropEnabled = false; // ファイルドロップ許可フラグ
        uint64_t m_nextMessageHandlerId = 1; // 次のメッセージハンドラ ID
        Core::CQRS::Bridge* m_platformBridge = nullptr; // Platform 用 command bridge
        std::vector<MessageHandlerEntry> m_messageHandlers; // メッセージハンドラエントリのリスト
        std::vector<std::string> m_droppedFiles{}; // ドロップされたファイルパス
    };
}
