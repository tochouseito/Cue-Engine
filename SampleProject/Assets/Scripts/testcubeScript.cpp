#include "testcubeScript.h"

// === Engine includes ===
#include <Script/ScriptClassRegistry.h>

namespace GameScript
{
    void testcubeScript::start() noexcept
    {
    }

    void testcubeScript::update(float) noexcept
    {
        constexpr float k_rotationSpeed = 1.0f;
        constexpr float k_rotationDirection = 1.0f;
        transform.rotation.y +=
            delta_time() * k_rotationSpeed * k_rotationDirection;
    }
} // namespace GameScript

// GameScriptModule が class 名を列挙し、Editor の ScriptComponent 選択肢に反映する
CUE_REGISTER_SCRIPT_CLASS("testcubeScript", GameScript::testcubeScript)
