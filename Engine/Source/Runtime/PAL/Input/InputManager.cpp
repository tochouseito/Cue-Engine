// 入力管理の翻訳単位を固定し、Platform 別入力実装との差し替え境界を残す

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
        m_previousMouseButtonStates.fill(false);
        m_mouseDelta = {};
        m_mousePosition = {};
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
        m_previousMouseButtonStates = m_mouseButtonStates;
        m_mouseButtonStates.fill(false);
        if (m_mouse != nullptr)
        {
            result = m_mouse->update();
            if (!result)
            {
                return result;
            }

            m_mouseDelta = m_mouse->delta();
            m_mousePosition = m_mouse->position();
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

    void InputManager::shutdown() noexcept
    {
        m_keyboard = nullptr;
        m_mouse = nullptr;
        m_keyStates.fill(false);
        m_mouseButtonStates.fill(false);
        m_previousMouseButtonStates.fill(false);
        m_mouseDelta = {};
        m_mousePosition = {};
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

    bool InputManager::push_mouse_button(MouseButton a_button) const noexcept
    {
        const size_t buttonIndex = static_cast<size_t>(a_button);
        if (buttonIndex >= m_mouseButtonStates.size())
        {
            return false;
        }

        return m_mouseButtonStates[buttonIndex];
    }

    bool InputManager::mouse_button_pressed(MouseButton a_button) const noexcept
    {
        const size_t buttonIndex = static_cast<size_t>(a_button);
        if (buttonIndex >= m_mouseButtonStates.size())
        {
            return false;
        }

        return m_mouseButtonStates[buttonIndex] &&
            !m_previousMouseButtonStates[buttonIndex];
    }

    bool InputManager::mouse_button_released(MouseButton a_button) const noexcept
    {
        const size_t buttonIndex = static_cast<size_t>(a_button);
        if (buttonIndex >= m_mouseButtonStates.size())
        {
            return false;
        }

        return !m_mouseButtonStates[buttonIndex] &&
            m_previousMouseButtonStates[buttonIndex];
    }
}
