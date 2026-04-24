#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include "Keyboard.h"

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

        [[nodiscard]] Result initialize(IKeyboard* a_keyboard) noexcept;
        [[nodiscard]] Result begin_frame() noexcept;
        void shutdown() noexcept;

        /// @brief 指定キーが押されていれば `true` を返します。
        [[nodiscard]] bool push_key(Key a_key) const noexcept;

    private:
        static constexpr size_t k_keyCount =
            static_cast<size_t>(Key::Count);

    private:
        IKeyboard* m_keyboard = nullptr;
        std::array<bool, k_keyCount> m_keyStates{};
    };
}
