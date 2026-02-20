#pragma once
#include <cstdint>
#include "frame/FrameController.h"

namespace Cue
{
    struct EngineConfig final
    {
        // 1) フレームコントローラーの設定をまとめる
        uint32_t m_bufferCount = 2;
        uint32_t m_maxFps = 60;
        ControllerMode m_mode = ControllerMode::Fixed;
    };
}
