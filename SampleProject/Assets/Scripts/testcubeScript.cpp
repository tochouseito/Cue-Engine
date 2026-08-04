#include "testcubeScript.h"

// === Engine includes ===
#include <Script/ScriptClassRegistry.h>

namespace GameScript
{
    void testcubeScript::start() noexcept
    {
    }

    void testcubeScript::update(float a_deltaTimeSeconds) noexcept
    {
        constexpr float k_rotationDirection = 1.0f;
        transform.rotation.y +=
            a_deltaTimeSeconds * rotationSpeed * k_rotationDirection;
        m_elapsedSeconds += a_deltaTimeSeconds;
    }

    void testcubeScript::reset_rotation() noexcept
    {
        transform.rotation = {};
    }

    void testcubeScript::toggle_height() noexcept
    {
        transform.position.y = transform.position.y < 0.5f ? 1.5f : 0.0f;
    }

    void testcubeScript::save_state(SavedState& a_outState) const noexcept
    {
        a_outState.elapsedSeconds = m_elapsedSeconds;
    }

    void testcubeScript::restore_state(const SavedState& a_state) noexcept
    {
        m_elapsedSeconds = a_state.elapsedSeconds;
    }
} // namespace GameScript

// GameScriptModule が class 名を列挙し、Editor の ScriptComponent 選択肢に反映する
CUE_REGISTER_SCRIPT_CLASS("testcubeScript", GameScript::testcubeScript)
