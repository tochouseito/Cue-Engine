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

        return Result::ok();
    }

    void WinMouse::shutdown() noexcept
    {
        if (m_mouseDevice != nullptr)
        {
            m_mouseDevice->Unacquire();
            m_mouseDevice.Reset();
        }

        m_directInput.Reset();
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

        m_delta.x = static_cast<int32_t>(state.lX);
        m_delta.y = static_cast<int32_t>(state.lY);
        m_delta.wheel = static_cast<int32_t>(state.lZ);
        for (size_t buttonIndex = 0; buttonIndex < m_buttonStates.size();
             ++buttonIndex)
        {
            m_buttonStates[buttonIndex] = state.rgbButtons[buttonIndex];
        }
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
}
