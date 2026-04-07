#include "GameCore.h"

namespace Cue
{
    Result GameCore::initialize()
    {
        m_ecsManager = std::make_unique<ECS::ECSManager>();
        m_ecsManager->add_system<ECS::ObjectInfoSystem>(m_renderSceneState);
        m_ecsManager->add_system<ECS::TransformSystem>(m_renderSceneState);
        return Result::ok();
    }

    Result GameCore::update(float a_deltaTime)
    {
        m_renderSceneState.frameState.objectCount =
            static_cast<uint32_t>(m_entities.size());
        m_renderSceneState.objectInfos.assign(m_entities.size(), {});
        m_renderSceneState.localTransforms.assign(m_entities.size(), {});

        for (size_t entityIndex = 0; entityIndex < m_entities.size(); ++entityIndex)
        {
            ECS::TransformComponent* transform =
                m_ecsManager->get_component<ECS::TransformComponent>(
                    m_entities[entityIndex]);
            if (transform == nullptr)
            {
                continue;
            }

            switch (entityIndex)
            {
            case 0:
                transform->rotation.m_y += a_deltaTime * 1.25f;
                break;
            case 1:
                transform->rotation.m_x += a_deltaTime * 0.75f;
                break;
            case 2:
                transform->rotation.m_y -= a_deltaTime * 1.0f;
                break;
            default:
                transform->rotation.m_y += a_deltaTime * 0.5f;
                break;
            }
        }

        m_ecsManager->update_all_systems();
        return Result::ok();
    }

    Result GameCore::add_object(const Math::float3& a_position)
    {
        ECS::Entity entity = m_ecsManager->generate_entity();

        ECS::ObjectInfoComponent* objectInfo =
            m_ecsManager->add_component<ECS::ObjectInfoComponent>(entity);
        ECS::TransformComponent* transform =
            m_ecsManager->add_component<ECS::TransformComponent>(entity);
        if (objectInfo == nullptr || transform == nullptr)
        {
            return Result::fail(Code::InternalError, Severity::Error,
                "Failed to add required components for object.");
        }

        const uint32_t objectIndex = static_cast<uint32_t>(m_entities.size());
        objectInfo->objectId = objectIndex;
        objectInfo->meshId = 0;
        objectInfo->transformId = objectIndex;
        objectInfo->visible = true;

        transform->position = a_position;
        transform->rotation = Math::float3::zero();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);

        m_entities.push_back(entity);
        m_renderSceneState.frameState.objectCount =
            static_cast<uint32_t>(m_entities.size());

        return Result::ok();
    }
} // namespace Cue
