#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::PAL
{
    class IPlatform;
    std::unique_ptr<IPlatform> create_platform();
}
