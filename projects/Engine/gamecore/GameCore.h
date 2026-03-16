#pragma once
#include <cstdint>
#include <Native/EngineNativeStruct.h>
#include <TransformBufferPool.h>

namespace Cue
{
    class GameCore final
    {
    public:
        GameCore() = default;
        ~GameCore() = default;

        Result initialize(GraphicsCore::TransformBufferPool& transformBufferPool);
        void update(uint64_t frameNo, uint32_t index);

        [[nodiscard]] GraphicsCore::TransformSlotHandle front_cube_transform_slot() const noexcept;
        [[nodiscard]] GraphicsCore::TransformSlotHandle back_cube_transform_slot() const noexcept;
    private:
        GraphicsCore::TransformBufferPool* m_transformBufferPool = nullptr;
        GraphicsCore::TransformSlotHandle m_frontCubeTransformSlot{};
        GraphicsCore::TransformSlotHandle m_backCubeTransformSlot{};
        Core::Native::LocalTransform m_cubeTransform{};
    };
}
