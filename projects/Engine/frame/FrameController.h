#pragma once

// === C++ Standard Library ===
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace Cue
{
    class FrameController
    {
    public:
        FrameController() = default;
        ~FrameController() = default;
    };
}
