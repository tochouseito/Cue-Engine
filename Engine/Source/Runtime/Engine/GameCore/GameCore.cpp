#include "GameCore.h"

namespace Cue
{
    Result GameCore::initialize()
    {
        m_ecsManager = std::make_unique<ECS::ECSManager>();
        m_ecsManager->add_system<ECS::ObjectInfoSystem>();
        m_ecsManager->add_system<ECS::TransformSystem>();
        return Result::ok();
    }
    Result GameCore::update()
    {
        m_ecsManager->update_all_systems();
        return Result::ok();
    }
    Result GameCore::add_object()
    {
        ECS::Entity entity = m_ecsManager->generate_entity();
        m_ecsManager->add_component<ECS::ObjectInfoComponent>(entity);
        m_ecsManager->add_component<ECS::TransformComponent>(entity);
        return Result::ok();
    }
} // namespace Cue
