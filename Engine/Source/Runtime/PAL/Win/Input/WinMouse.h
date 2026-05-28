// WinMouse の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include <Input/Mouse.h>

// === C++ includes ===
#include <array>

// === Windows API includes ===
#include "../stdafx.h"

namespace Cue::PAL::Win
{
    class WinMouse final : public IMouse
    {
    public:
        Result initialize(HINSTANCE a_instanceHandle, HWND a_windowHandle);
        void shutdown() noexcept;
        [[nodiscard]] Result update() noexcept override;

        [[nodiscard]] MouseDelta delta() const noexcept override
        {
            return m_delta;
        }

        [[nodiscard]] MousePosition position() const noexcept override
        {
            return m_position;
        }

        [[nodiscard]] bool is_button_down(MouseButton a_button) const noexcept override;

    private:
        std::array<std::uint8_t, static_cast<size_t>(MouseButton::Count)>
            m_buttonStates{};
        HWND m_windowHandle = nullptr;
        MouseDelta m_delta{};
        MousePosition m_position{};
        Microsoft::WRL::ComPtr<IDirectInput8W> m_directInput = nullptr;
        Microsoft::WRL::ComPtr<IDirectInputDevice8W> m_mouseDevice = nullptr;
    };
}
