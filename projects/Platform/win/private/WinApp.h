#pragma once
// c++ 標準ライブラリ include
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// cue engine 関連 include
#include "Result.h"
#include "win_native.h"

namespace Cue::Platform::Win
{
    class WinApp final
    {
    public:
        using MessageHandler = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)>;

        /// @brief コンストラクタ
        WinApp();
        /// @brief デストラクタ
        ~WinApp();

        /// @brief ウィンドウの作成
        [[nodiscard]] Result create_window(uint32_t w, uint32_t h, const wchar_t* className, const wchar_t* titleName);
        /// @brief ウィンドウの破棄
        [[nodiscard]] Result destroy_window();
        /// @brief ウィンドウの表示
        [[nodiscard]] Result show_window(bool isMaximized);
        /// @brief ウィンドウのメッセージポンプ
        [[nodiscard]] bool pump_messages();
        /// @brief native window handle 取得
        [[nodiscard]] NativeWindowHandle get_native_window_handle() const noexcept;
        /// @brief メッセージハンドラ登録
        [[nodiscard]] uint64_t register_message_handler(MessageHandler handler);
        /// @brief メッセージハンドラ解除
        bool unregister_message_handler(uint64_t handlerId);

        /// @brief ウィンドウ幅取得
        uint32_t get_window_width() const noexcept;
        /// @brief ウィンドウ高さ取得
        uint32_t get_window_height() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
