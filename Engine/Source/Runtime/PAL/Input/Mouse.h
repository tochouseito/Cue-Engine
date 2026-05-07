#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    enum class MouseButton : uint8_t
    {
        Left,
        Right,
        Middle,
        X1,
        X2,
        Count,
    };

    struct MouseDelta final
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t wheel = 0;
    };

    class IMouse
    {
    public:
        virtual ~IMouse() = default;

        [[nodiscard]] virtual Result update() noexcept = 0;
        [[nodiscard]] virtual MouseDelta delta() const noexcept = 0;
        [[nodiscard]] virtual bool is_button_down(MouseButton a_button) const noexcept = 0;
    };
}
