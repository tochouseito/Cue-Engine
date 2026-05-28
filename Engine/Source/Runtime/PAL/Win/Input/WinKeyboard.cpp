#include "WinKeyboard.h"

namespace Cue::PAL::Win
{
    namespace
    {
        constexpr std::uint8_t k_invalidDirectInputKey = 0xFF;
    }

    Result WinKeyboard::initialize(HINSTANCE a_instanceHandle, HWND a_windowHandle)
    {
        if (a_instanceHandle == nullptr || a_windowHandle == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "DirectInput keyboard initialization requires valid window handles.");
        }

        HRESULT hr = ::DirectInput8Create(a_instanceHandle, DIRECTINPUT_VERSION,
            IID_IDirectInput8W, reinterpret_cast<void**>(m_directInput.GetAddressOf()),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(Code::InitializeFailed, Severity::Fatal,
                "Failed to create DirectInput interface.");
        }

        hr = m_directInput->CreateDevice(GUID_SysKeyboard,
            m_keyboardDevice.GetAddressOf(), nullptr);
        if (FAILED(hr))
        {
            return Result::fail(Code::CreateFailed, Severity::Fatal,
                "Failed to create DirectInput keyboard device.");
        }

        hr = m_keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
        if (FAILED(hr))
        {
            return Result::fail(Code::InitializeFailed, Severity::Fatal,
                "Failed to set DirectInput keyboard data format.");
        }

        hr = m_keyboardDevice->SetCooperativeLevel(a_windowHandle,
            DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        if (FAILED(hr))
        {
            return Result::fail(Code::InitializeFailed, Severity::Fatal,
                "Failed to set DirectInput keyboard cooperative level.");
        }

        return Result::ok();
    }

    void WinKeyboard::shutdown() noexcept
    {
        if (m_keyboardDevice != nullptr)
        {
            m_keyboardDevice->Unacquire();
            m_keyboardDevice.Reset();
        }

        m_directInput.Reset();
        m_keyStates.fill(0);
    }

    Result WinKeyboard::update() noexcept
    {
        if (m_keyboardDevice == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "DirectInput keyboard device is not initialized.");
        }

        m_keyStates.fill(0);

        HRESULT hr = m_keyboardDevice->GetDeviceState(
            static_cast<DWORD>(m_keyStates.size()), m_keyStates.data());
        if (SUCCEEDED(hr))
        {
            return Result::ok();
        }

        hr = m_keyboardDevice->Acquire();
        while (hr == DIERR_INPUTLOST)
        {
            hr = m_keyboardDevice->Acquire();
        }

        // フォーカス喪失中は取得不能でも正常系として扱い、押下状態を空に保つ
        if (FAILED(hr))
        {
            return Result::ok();
        }

        hr = m_keyboardDevice->GetDeviceState(
            static_cast<DWORD>(m_keyStates.size()), m_keyStates.data());
        if (FAILED(hr))
        {
            return Result::ok();
        }

        return Result::ok();
    }

    bool WinKeyboard::is_key_down(Key a_key) const noexcept
    {
        const uint8_t directInputKey = to_direct_input_key(a_key);
        if (directInputKey == k_invalidDirectInputKey)
        {
            return false;
        }

        return (m_keyStates[directInputKey] & 0x80u) != 0;
    }

    uint8_t WinKeyboard::to_direct_input_key(Key a_key) noexcept
    {
        switch (a_key)
        {
        case Key::Escape: return DIK_ESCAPE;
        case Key::Tab: return DIK_TAB;
        case Key::CapsLock: return DIK_CAPITAL;
        case Key::LeftShift: return DIK_LSHIFT;
        case Key::RightShift: return DIK_RSHIFT;
        case Key::LeftControl: return DIK_LCONTROL;
        case Key::RightControl: return DIK_RCONTROL;
        case Key::LeftAlt: return DIK_LMENU;
        case Key::RightAlt: return DIK_RMENU;
        case Key::Space: return DIK_SPACE;
        case Key::Enter: return DIK_RETURN;
        case Key::Backspace: return DIK_BACK;
        case Key::Insert: return DIK_INSERT;
        case Key::Delete: return DIK_DELETE;
        case Key::Home: return DIK_HOME;
        case Key::End: return DIK_END;
        case Key::PageUp: return DIK_PRIOR;
        case Key::PageDown: return DIK_NEXT;
        case Key::Left: return DIK_LEFT;
        case Key::Right: return DIK_RIGHT;
        case Key::Up: return DIK_UP;
        case Key::Down: return DIK_DOWN;
        case Key::Num0: return DIK_0;
        case Key::Num1: return DIK_1;
        case Key::Num2: return DIK_2;
        case Key::Num3: return DIK_3;
        case Key::Num4: return DIK_4;
        case Key::Num5: return DIK_5;
        case Key::Num6: return DIK_6;
        case Key::Num7: return DIK_7;
        case Key::Num8: return DIK_8;
        case Key::Num9: return DIK_9;
        case Key::A: return DIK_A;
        case Key::B: return DIK_B;
        case Key::C: return DIK_C;
        case Key::D: return DIK_D;
        case Key::E: return DIK_E;
        case Key::F: return DIK_F;
        case Key::G: return DIK_G;
        case Key::H: return DIK_H;
        case Key::I: return DIK_I;
        case Key::J: return DIK_J;
        case Key::K: return DIK_K;
        case Key::L: return DIK_L;
        case Key::M: return DIK_M;
        case Key::N: return DIK_N;
        case Key::O: return DIK_O;
        case Key::P: return DIK_P;
        case Key::Q: return DIK_Q;
        case Key::R: return DIK_R;
        case Key::S: return DIK_S;
        case Key::T: return DIK_T;
        case Key::U: return DIK_U;
        case Key::V: return DIK_V;
        case Key::W: return DIK_W;
        case Key::X: return DIK_X;
        case Key::Y: return DIK_Y;
        case Key::Z: return DIK_Z;
        case Key::F1: return DIK_F1;
        case Key::F2: return DIK_F2;
        case Key::F3: return DIK_F3;
        case Key::F4: return DIK_F4;
        case Key::F5: return DIK_F5;
        case Key::F6: return DIK_F6;
        case Key::F7: return DIK_F7;
        case Key::F8: return DIK_F8;
        case Key::F9: return DIK_F9;
        case Key::F10: return DIK_F10;
        case Key::F11: return DIK_F11;
        case Key::F12: return DIK_F12;
        default:
            return k_invalidDirectInputKey;
        }
    }
}
