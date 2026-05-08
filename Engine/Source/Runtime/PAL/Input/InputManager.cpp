#include "InputManager.h"

namespace Cue::PAL
{
    Result InputManager::initialize(IKeyboard* a_keyboard, IMouse* a_mouse) noexcept
    {
        if (a_keyboard == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Input manager requires a valid keyboard.");
        }

        m_keyboard = a_keyboard;
        m_mouse = a_mouse;
        m_keyStates.fill(false);
        m_mouseButtonStates.fill(false);
        m_mouseDelta = {};
        return Result::ok();
    }

    Result InputManager::begin_frame() noexcept
    {
        if (m_keyboard == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Input manager is not initialized.");
        }

        Result result = m_keyboard->update();
        if (!result)
        {
            return result;
        }

        m_mouseDelta = {};
        m_mouseButtonStates.fill(false);
        if (m_mouse != nullptr)
        {
            result = m_mouse->update();
            if (!result)
            {
                return result;
            }

            m_mouseDelta = m_mouse->delta();
            for (size_t buttonIndex = 0;
                 buttonIndex < m_mouseButtonStates.size(); ++buttonIndex)
            {
                m_mouseButtonStates[buttonIndex] =
                    m_mouse->is_button_down(
                        static_cast<MouseButton>(buttonIndex));
            }
        }

        m_keyStates[static_cast<size_t>(Key::Unknown)] = false;
        for (size_t keyIndex = 1; keyIndex < k_keyCount; ++keyIndex)
        {
            const Key key = static_cast<Key>(keyIndex);
            m_keyStates[keyIndex] = m_keyboard->is_key_down(key);
        }

        return Result::ok();
    }

    Result InputManager::set_mouse_capture_enabled(bool a_isEnabled) noexcept
    {
        if (m_mouse == nullptr)
        {
            return Result::ok();
        }

        Result result = m_mouse->set_capture_enabled(a_isEnabled);
        if (!result)
        {
            return result;
        }
        if (!a_isEnabled)
        {
            m_mouseDelta = {};
        }
        return Result::ok();
    }

    void InputManager::shutdown() noexcept
    {
        (void)set_mouse_capture_enabled(false);
        m_keyboard = nullptr;
        m_mouse = nullptr;
        m_keyStates.fill(false);
        m_mouseButtonStates.fill(false);
        m_mouseDelta = {};
    }

    bool InputManager::push_key(Key a_key) const noexcept
    {
        const size_t keyIndex = static_cast<size_t>(a_key);
        if (keyIndex >= k_keyCount)
        {
            return false;
        }

        return m_keyStates[keyIndex];
    }

    bool InputManager::is_mouse_capture_enabled() const noexcept
    {
        return m_mouse != nullptr && m_mouse->is_capture_enabled();
    }

    bool InputManager::push_mouse_button(MouseButton a_button) const noexcept
    {
        const size_t buttonIndex = static_cast<size_t>(a_button);
        if (buttonIndex >= m_mouseButtonStates.size())
        {
            return false;
        }

        return m_mouseButtonStates[buttonIndex];
    }
}
