#pragma once

#include "testcubeScript.h"

#include <Script/MarionnetteBehaviour.h>

namespace GameScript
{
    class testScriptScript final : public Cue::Script::MarionnetteBehaviour
    {
    public:
        using MarionnetteBehaviour::MarionnetteBehaviour;

        Cue::Script::ScriptObjectPtr<testcubeScript> targetScript{};

        void start() noexcept override;
        void update(float a_deltaTimeSeconds) noexcept override;
        void invoke_target() noexcept;

        MARIONETTE_FIELDS(
            MARIONETTE_FIELD(
                testScriptScript, targetScript,
                Cue::Script::ScriptObjectPtr<testcubeScript>{}))
        MARIONETTE_FUNCTIONS(
            MARIONETTE_FUNCTION(testScriptScript, invoke_target))

    private:
        float m_resetTimer = 2.0f;
    };
} // namespace GameScript

MARIONETTE_DECLARE_SCRIPT_TYPE(
    GameScript::testScriptScript, "testScriptScript");
