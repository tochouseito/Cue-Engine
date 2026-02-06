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
        [[nodiscard]] Core::Result create_window(uint32_t w, uint32_t h);
        /// @brief ウィンドウの破棄
        [[nodiscard]] Core::Result destroy_window();
        /// @brief ウィンドウの表示
        [[nodiscard]] Core::Result show_window(bool isMaximized);
    private:
        [[nodiscard]] bool pump_messages();
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
