#include "WinMouse.h"

namespace Cue::PAL::Win
{
    Result WinMouse::initialize(HINSTANCE a_instanceHandle, HWND a_windowHandle)
    {
        if (a_instanceHandle == nullptr || a_windowHandle == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "DirectInput mouse initialization requires valid window handles.");
        }

        HRESULT hr = ::DirectInput8Create(a_instanceHandle, DIRECTINPUT_VERSION,
            IID_IDirectInput8W,
            reinterpret_cast<void**>(m_directInput.GetAddressOf()),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(Code::InitializeFailed, Severity::Fatal,
                "Failed to create DirectInput interface for mouse.");
        }

        hr = m_directInput->CreateDevice(
            GUID_SysMouse, m_mouseDevice.GetAddressOf(), nullptr);
        if (FAILED(hr))
        {
            return Result::fail(Code::CreateFailed, Severity::Fatal,
                "Failed to create DirectInput mouse device.");
        }

        hr = m_mouseDevice->SetDataFormat(&c_dfDIMouse2);
        if (FAILED(hr))
        {
            return Result::fail(Code::InitializeFailed, Severity::Fatal,
                "Failed to set DirectInput mouse data format.");
        }

        hr = m_mouseDevice->SetCooperativeLevel(a_windowHandle,
            DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        if (FAILED(hr))
        {
            return Result::fail(Code::InitializeFailed, Severity::Fatal,
                "Failed to set DirectInput mouse cooperative level.");
        }

        m_windowHandle = a_windowHandle;
        return Result::ok();
    }

    void WinMouse::shutdown() noexcept
    {
        (void)set_capture_enabled(false);
        if (m_mouseDevice != nullptr)
        {
            m_mouseDevice->Unacquire();
            m_mouseDevice.Reset();
        }

        m_directInput.Reset();
        m_windowHandle = nullptr;
        m_buttonStates.fill(0);
        m_delta = {};
    }

    Result WinMouse::update() noexcept
    {
        if (m_mouseDevice == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "DirectInput mouse device is not initialized.");
        }
        if (m_isCaptureEnabled && ::GetForegroundWindow() != m_windowHandle)
        {
            (void)set_capture_enabled(false);
        }

        DIMOUSESTATE2 state{};
        HRESULT hr = m_mouseDevice->GetDeviceState(sizeof(state), &state);
        if (FAILED(hr))
        {
            hr = m_mouseDevice->Acquire();
            while (hr == DIERR_INPUTLOST)
            {
                hr = m_mouseDevice->Acquire();
            }

            if (FAILED(hr))
            {
                m_buttonStates.fill(0);
                m_delta = {};
                return Result::ok();
            }

            hr = m_mouseDevice->GetDeviceState(sizeof(state), &state);
            if (FAILED(hr))
            {
                m_buttonStates.fill(0);
                m_delta = {};
                return Result::ok();
            }
        }

        m_delta.x = m_isCaptureEnabled ? static_cast<int32_t>(state.lX) : 0;
        m_delta.y = m_isCaptureEnabled ? static_cast<int32_t>(state.lY) : 0;
        m_delta.wheel = static_cast<int32_t>(state.lZ);
        for (size_t buttonIndex = 0; buttonIndex < m_buttonStates.size();
             ++buttonIndex)
        {
            m_buttonStates[buttonIndex] = state.rgbButtons[buttonIndex];
        }
        if (m_isCaptureEnabled)
        {
            update_clip_rect();
            center_cursor();
        }
        return Result::ok();
    }

    Result WinMouse::set_capture_enabled(bool a_isEnabled) noexcept
    {
        if (m_isCaptureEnabled == a_isEnabled)
        {
            return Result::ok();
        }
        if (a_isEnabled && m_windowHandle == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Mouse capture requires a valid window handle.");
        }

        m_isCaptureEnabled = a_isEnabled;
        m_delta = {};
        if (m_isCaptureEnabled)
        {
            ::SetCapture(m_windowHandle);
            update_clip_rect();
            center_cursor();
            set_cursor_visible(false);
            if (m_mouseDevice != nullptr)
            {
                (void)m_mouseDevice->Acquire();
            }
            return Result::ok();
        }

        ::ClipCursor(nullptr);
        if (::GetCapture() == m_windowHandle)
        {
            ::ReleaseCapture();
        }
        set_cursor_visible(true);
        return Result::ok();
    }

    bool WinMouse::is_button_down(MouseButton a_button) const noexcept
    {
        const size_t buttonIndex = static_cast<size_t>(a_button);
        if (buttonIndex >= m_buttonStates.size())
        {
            return false;
        }

        return (m_buttonStates[buttonIndex] & 0x80u) != 0;
    }

    void WinMouse::update_clip_rect() noexcept
    {
        if (m_windowHandle == nullptr)
        {
            return;
        }

        RECT clientRect{};
        if (!::GetClientRect(m_windowHandle, &clientRect))
        {
            return;
        }

        POINT topLeft{ clientRect.left, clientRect.top };
        POINT bottomRight{ clientRect.right, clientRect.bottom };
        if (!::ClientToScreen(m_windowHandle, &topLeft) ||
            !::ClientToScreen(m_windowHandle, &bottomRight))
        {
            return;
        }

        RECT screenRect{
            topLeft.x,
            topLeft.y,
            bottomRight.x,
            bottomRight.y
        };
        ::ClipCursor(&screenRect);
    }

    void WinMouse::center_cursor() noexcept
    {
        if (m_windowHandle == nullptr)
        {
            return;
        }

        RECT clientRect{};
        if (!::GetClientRect(m_windowHandle, &clientRect))
        {
            return;
        }

        POINT center{
            (clientRect.left + clientRect.right) / 2,
            (clientRect.top + clientRect.bottom) / 2
        };
        if (::ClientToScreen(m_windowHandle, &center))
        {
            ::SetCursorPos(center.x, center.y);
        }
    }

    void WinMouse::set_cursor_visible(bool a_isVisible) noexcept
    {
        int displayCount = ::ShowCursor(a_isVisible ? TRUE : FALSE);
        if (a_isVisible)
        {
            while (displayCount < 0)
            {
                displayCount = ::ShowCursor(TRUE);
            }
            return;
        }

        while (displayCount >= 0)
        {
            displayCount = ::ShowCursor(FALSE);
        }
    }
}
