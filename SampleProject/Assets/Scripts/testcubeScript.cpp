#include "testcubeScript.h"

// === Engine includes ===
#include <Script/ScriptClassRegistry.h>

// === C++ includes ===
#include <cmath>

namespace GameScript
{
    void testcubeScript::start() noexcept
    {
    }

    void testcubeScript::update(float a_deltaTimeSeconds) noexcept
    {
        Cue::Core::Native::ScriptTransformQuaternion transform{};
        if (get_transform(transform) != Cue::Core::Native::ScriptAbiResult::Ok)
        {
            return;
        }

        constexpr float k_rotationRadiansPerSecond = 1.0f;
        const float halfAngle = 0.5f * k_rotationRadiansPerSecond * a_deltaTimeSeconds;
        const float sine = std::sin(halfAngle);
        const float cosine = std::cos(halfAngle);
        const Cue::Core::Native::ScriptQuaternion rotation = transform.rotation;
        transform.rotation = {
            cosine * rotation.x + sine * rotation.z,
            cosine * rotation.y + sine * rotation.w,
            cosine * rotation.z - sine * rotation.x,
            cosine * rotation.w - sine * rotation.y};
        (void)set_transform(transform);
    }
} // namespace GameScript

// GameScriptModule が class 名を列挙し、Editor の ScriptComponent 選択肢に反映する
CUE_REGISTER_SCRIPT_CLASS("testcubeScript", GameScript::testcubeScript)
