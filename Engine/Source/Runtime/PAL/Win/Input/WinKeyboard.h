#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include <Input/Keyboard.h>

// === C++ includes ===
#include <array>

// === Windows API includes ===
#include "../stdafx.h"

namespace Cue::PAL::Win
{
    /// @brief DirectInput を使った Windows キーボード入力実装です。
    class WinKeyboard final : public IKeyboard
    {
    public:
        Result initialize(HINSTANCE a_instanceHandle, HWND a_windowHandle);
        void shutdown() noexcept;
        Result update() noexcept;

        [[nodiscard]] bool is_key_down(Key a_key) const noexcept override;

    private:
        [[nodiscard]] static uint8_t to_direct_input_key(Key a_key) noexcept;

    private:
        std::array<std::uint8_t, 256> m_keyStates{};
        Microsoft::WRL::ComPtr<IDirectInput8W> m_directInput = nullptr;
        Microsoft::WRL::ComPtr<IDirectInputDevice8W> m_keyboardDevice = nullptr;
    };
}
