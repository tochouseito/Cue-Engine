#include "GameCore.h"

namespace Cue
{
    Result GameCore::initialize()
    {
        m_ecsManager = std::make_unique<ECS::ECSManager>();
        return Result::ok();
    }
}
