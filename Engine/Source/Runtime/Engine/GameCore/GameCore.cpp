#include "GameCore.h"

namespace Cue
{
    Math::float3 GameCore::make_spawn_position() const noexcept
    {
        const size_t objectIndex = m_entities.size();
        const uint32_t column = static_cast<uint32_t>(objectIndex % 3u);
        const uint32_t row = static_cast<uint32_t>(objectIndex / 3u);

        return Math::float3{
            (static_cast<float>(column) - 1.0f) * 2.0f,
            0.0f,
            static_cast<float>(row) * 2.5f
        };
    }

    void GameCore::rebuild_object_indices()
    {
        for (size_t entityIndex = 0; entityIndex < m_entities.size(); ++entityIndex)
        {
            ECS::ObjectInfoComponent* objectInfo =
                m_ecsManager->get_component<ECS::ObjectInfoComponent>(m_entities[entityIndex]);
            if (objectInfo == nullptr)
            {
                continue;
            }

            const uint32_t objectId = static_cast<uint32_t>(entityIndex);
            objectInfo->objectId = objectId;
            objectInfo->transformId = objectId;
            objectInfo->meshId = 0;
        }
    }

    Result GameCore::initialize()
    {
        m_worldResources = std::make_unique<WorldResources>();

        constexpr uint32_t k_maxObjectCount = 1000;
        m_worldResources->create_object_info_buffer(k_maxObjectCount);
        m_worldResources->create_transform_buffer(k_maxObjectCount);
        m_worldResources->create_render_object_buffer(k_maxObjectCount);
        m_worldResources->create_object_count_buffer();

        m_ecsManager = std::make_unique<ECS::ECSManager>();
        m_ecsManager->add_system<ECS::ObjectInfoSystem>(m_renderSceneState, m_worldResources->object_info_uploaders());
        m_ecsManager->add_system<ECS::TransformSystem>(m_renderSceneState, m_worldResources->transform_uploaders());
        
        return Result::ok();
    }

    Result GameCore::update(float a_deltaTime)
    {
        m_renderSceneState.frameState.objectCount = static_cast<uint32_t>(m_entities.size());
        m_renderSceneState.objectInfos.assign(m_entities.size(), {});
        m_renderSceneState.localTransforms.assign(m_entities.size(), {});

        for (size_t entityIndex = 0; entityIndex < m_entities.size(); ++entityIndex)
        {
            ECS::TransformComponent* transform =
                m_ecsManager->get_component<ECS::TransformComponent>(m_entities[entityIndex]);
            if (transform == nullptr)
            {
                continue;
            }

            switch (entityIndex)
            {
            case 0:
                transform->rotation.y += a_deltaTime * 1.25f;
                break;
            case 1:
                transform->rotation.x += a_deltaTime * 0.75f;
                break;
            case 2:
                transform->rotation.y -= a_deltaTime * 1.0f;
                break;
            default:
                transform->rotation.y += a_deltaTime * 0.5f;
                break;
            }
        }

        m_ecsManager->update_all_systems();
        return Result::ok();
    }

    Result GameCore::add_object()
    {
        return add_object(make_spawn_position());
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
            return Result::fail(
                Code::InternalError,
                Severity::Error,
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
        m_renderSceneState.frameState.objectCount = static_cast<uint32_t>(m_entities.size());

        return Result::ok();
    }

    Result GameCore::remove_object(uint32_t objectId)
    {
        if (objectId >= m_entities.size())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Object id was not found.");
        }

        const size_t objectIndex = static_cast<size_t>(objectId);
        const ECS::Entity entity = m_entities[objectIndex];
        m_ecsManager->remove_entity(entity);
        m_entities.erase(m_entities.begin() + static_cast<std::ptrdiff_t>(objectIndex));
        rebuild_object_indices();
        m_renderSceneState.frameState.objectCount = static_cast<uint32_t>(m_entities.size());

        return Result::ok();
    }
} // namespace Cue
