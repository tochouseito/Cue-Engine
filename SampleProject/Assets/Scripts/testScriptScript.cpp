#include "testScriptScript.h"

// === Engine includes ===
#include <Script/ScriptClassRegistry.h>

namespace GameScript
{
    void testScriptScript::start() noexcept
    {
    }

    void testScriptScript::update(float a_deltaTimeSeconds) noexcept
    {
        m_resetTimer -= a_deltaTimeSeconds;
        if (m_resetTimer <= 0.0f)
        {
            invoke_target();
            m_resetTimer = 2.0f;
        }
    }

    void testScriptScript::invoke_target() noexcept
    {
        const Cue::Script::ScriptRef<testcubeScript> target =
            targetScript.script_ref();
        if (target)
        {
            (void)target.invoke("toggle_height");
        }
    }
} // namespace GameScript

// GameScriptModule が class 名を列挙し、Editor の ScriptComponent 選択肢に反映する
CUE_REGISTER_SCRIPT_CLASS("testScriptScript", GameScript::testScriptScript)
