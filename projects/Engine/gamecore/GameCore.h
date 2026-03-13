#pragma once
#include <cstdint>
#include <Native/EngineNativeStruct.h>

namespace Cue
{
    class GameCore final
    {
    public:
        GameCore() = default;
        ~GameCore() = default;

        void update(uint64_t frameNo, uint32_t index)
        {
            (void)frameNo;
            (void)index;
        }
    private:
        Core::Native::LocalTransform m_localTransform{};
    };
}
