#pragma once
// C++ standard library includes
#include <cstdint>
#include <memory>

// Cue engine includes
#include "Result.h"

namespace Cue::Platform::Win
{
    class WinApp final
    {
    public:
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
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
