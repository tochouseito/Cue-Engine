#include "InputManager.h"

namespace Cue::PAL
{
    Result InputManager::initialize(IKeyboard* a_keyboard) noexcept
    {
        if (a_keyboard == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Input manager requires a valid keyboard.");
        }

        m_keyboard = a_keyboard;
        m_keyStates.fill(false);
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
        m_keyStates.fill(false);
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
}
