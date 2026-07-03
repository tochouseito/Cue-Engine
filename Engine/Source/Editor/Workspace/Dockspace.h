#pragma once

/// ************************************************************************************
/// ビューポート全体をカバーするドックスペース
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

namespace Cue::Editor
{
    /// @brief ビューポート全体をカバーするドックスペース
    class Dockspace final
    {
    public:
        using MenuCallback = void (*)(void* a_context);

        Dockspace() = default;
        ~Dockspace() = default;

        /// @brief メインメニューバーの View メニュー項目を描画する callback を設定する。
        void set_view_menu_callback(void* a_context, MenuCallback a_callback) noexcept
        {
            m_viewMenuContext = a_context;
            m_viewMenuCallback = a_callback;
        }

        void update();
    private:
        void* m_viewMenuContext = nullptr;
        MenuCallback m_viewMenuCallback = nullptr;
    };
}
