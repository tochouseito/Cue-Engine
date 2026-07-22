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
        (void)a_deltaTimeSeconds;
    }
} // namespace GameScript

// GameScriptModule が class 名を列挙し、Editor の ScriptComponent 選択肢に反映する
CUE_REGISTER_SCRIPT_CLASS("testScriptScript", GameScript::testScriptScript)
