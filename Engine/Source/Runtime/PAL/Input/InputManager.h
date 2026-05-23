#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include "Keyboard.h"
#include "Mouse.h"

// === C++ includes ===
#include <array>
#include <cstddef>

namespace Cue::PAL
{
    /// @brief 入力状態をフレーム単位で保持するマネージャです。
    class InputManager final
    {
    public:
        InputManager() = default;

        [[nodiscard]] Result initialize(IKeyboard* a_keyboard,
            IMouse* a_mouse = nullptr) noexcept;
        [[nodiscard]] Result begin_frame() noexcept;
        void shutdown() noexcept;

        /// @brief 指定キーが押されていれば `true` を返します。
        [[nodiscard]] bool push_key(Key a_key) const noexcept;
        /// @brief 指定マウスボタンが押されていれば `true` を返します。
        [[nodiscard]] bool push_mouse_button(MouseButton a_button) const noexcept;
        /// @brief 指定マウスボタンがこのフレームで押されたら `true` を返します。
        [[nodiscard]] bool mouse_button_pressed(MouseButton a_button) const noexcept;
        /// @brief 指定マウスボタンがこのフレームで離されたら `true` を返します。
        [[nodiscard]] bool mouse_button_released(MouseButton a_button) const noexcept;
        /// @brief 前フレームからのマウス移動量を返します。
        [[nodiscard]] MouseDelta mouse_delta() const noexcept
        {
            return m_mouseDelta;
        }
        /// @brief クライアント領域内のマウス座標を返します。
        [[nodiscard]] MousePosition mouse_position() const noexcept
        {
            return m_mousePosition;
        }

    private:
        static constexpr size_t k_keyCount =
            static_cast<size_t>(Key::Count);

    private:
        IKeyboard* m_keyboard = nullptr;
        IMouse* m_mouse = nullptr;
        std::array<bool, k_keyCount> m_keyStates{};
        std::array<bool, static_cast<size_t>(MouseButton::Count)> m_mouseButtonStates{};
        std::array<bool, static_cast<size_t>(MouseButton::Count)>
            m_previousMouseButtonStates{};
        MouseDelta m_mouseDelta{};
        MousePosition m_mousePosition{};
    };
}
