#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::RHI
{
    class IBackend;
    std::unique_ptr<IBackend> create_backend();
}
