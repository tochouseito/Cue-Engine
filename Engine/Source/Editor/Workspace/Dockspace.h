#pragma once

/// ************************************************************************************
/// ビューポート全体をカバーするドックスペース
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

namespace Cue::Editor
{
    /// @brief ビューポート全体をカバーするドックスペース
    class Dockspace final
    {
    public:
        Dockspace() = default;
        ~Dockspace() = default;

        void update();
    private:
    };
}
