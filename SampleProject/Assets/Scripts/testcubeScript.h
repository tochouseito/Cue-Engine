#pragma once

#include <Script/Marionnette.h>

namespace GameScript
{
    class testcubeScript final : public Cue::Script::MarionnetteComponent
    {
    public:
        void start() noexcept override;
        void update(float a_deltaTimeSeconds) noexcept override;
    };
} // namespace GameScript
