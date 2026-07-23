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
        constexpr float k_rotationRadiansPerSecond = 1.0f;
        const Cue::Core::Native::ScriptVector3 rotation{
            0.0f, k_rotationRadiansPerSecond * a_deltaTimeSeconds, 0.0f};
        (void)rotate(rotation);
    }
} // namespace GameScript

// GameScriptModule が class 名を列挙し、Editor の ScriptComponent 選択肢に反映する
CUE_REGISTER_SCRIPT_CLASS("testcubeScript", GameScript::testcubeScript)
