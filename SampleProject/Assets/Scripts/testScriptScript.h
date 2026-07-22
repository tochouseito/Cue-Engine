#pragma once

#include <Script/MarionnetteBehaviour.h>

namespace GameScript
{
    class testScriptScript final : public Cue::Script::MarionnetteBehaviour
    {
    public:
        using MarionnetteBehaviour::MarionnetteBehaviour;

        void start() noexcept override;
        void update(float a_deltaTimeSeconds) noexcept override;
    };
} // namespace GameScript
