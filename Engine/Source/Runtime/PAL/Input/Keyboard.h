#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    /// @brief キーボードキーの識別子です。
    enum class Key : uint8_t
    {
        Unknown = 0,
        Escape,
        Tab,
        CapsLock,
        LeftShift,
        RightShift,
        LeftControl,
        RightControl,
        LeftAlt,
        RightAlt,
        Space,
        Enter,
        Backspace,
        Insert,
        Delete,
        Home,
        End,
        PageUp,
        PageDown,
        Left,
        Right,
        Up,
        Down,
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        Count,
    };

    /// @brief キーボード入力の共通インターフェースです。
    class IKeyboard
    {
    public:
        virtual ~IKeyboard() = default;

        /// @brief デバイス状態を更新します。
        [[nodiscard]] virtual Result update() noexcept = 0;

        /// @brief 指定キーが押下中か返します。
        [[nodiscard]] virtual bool is_key_down(Key a_key) const noexcept = 0;
    };
}
