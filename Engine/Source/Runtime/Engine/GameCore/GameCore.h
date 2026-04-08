#pragma once

// === Base includes ===
#include <Result.h>

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include "Systems/ObjectInfoSystem.h"
#include "Systems/TransformSystem.h"

// === Engine includes ===
#include "WorldResources.h"

// === C++ includes ===
#include <memory>
#include <vector>

namespace Cue
{
    class GameCore final
    {
    public:
        GameCore() = default;
        ~GameCore() = default;

        Result initialize(RHI::IBufferManager* a_bufferManager,
            RHI::IViewManager* a_viewManager);
        Result update(float a_deltaTime, const uint32_t a_bufferIndex);

        Result add_object();
        Result add_object(const Math::float3& a_position);
        Result remove_object(uint32_t objectId);

    private:
        [[nodiscard]] Math::float3 make_spawn_position() const noexcept;
        void rebuild_object_indices();

    private:
        std::unique_ptr<ECS::ECSManager> m_ecsManager = nullptr;
        std::unique_ptr<WorldResources> m_worldResources = nullptr;
        std::vector<ECS::Entity> m_entities{};
    };
} // namespace Cue
