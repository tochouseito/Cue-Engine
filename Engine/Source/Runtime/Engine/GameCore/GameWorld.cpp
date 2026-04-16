#include "GameWorld.h"

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] ObjectDefinition make_default_static_mesh_object_definition(
            const Math::float3& a_position, uint32_t a_meshId)
        {
            ObjectDefinition objectDefinition("StaticMeshObject");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::float3::zero();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::MeshFilterComponent meshFilter{};
            meshFilter.meshId = a_meshId;
            objectDefinition.prototype.add_component(meshFilter);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.materialId = 0;
            renderer.visible = true;
            objectDefinition.prototype.add_component(renderer);

            return objectDefinition;
        }
    }

    [[nodiscard]] Result GameWorld::initialize(RHI::IBufferManager* a_bufferManager,
        RHI::IViewManager* a_viewManager, uint32_t a_bufferCount,
        uint32_t a_renderWidth, uint32_t a_renderHeight,
        uint32_t a_defaultStaticMeshId)
    {
        if (a_bufferManager == nullptr || a_viewManager == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld requires valid buffer and view managers.");
        }
        if (a_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }

        m_defaultStaticMeshId = a_defaultStaticMeshId;
        m_renderSceneState.resize(a_bufferCount);
        for (uint32_t bufferIndex = 0; bufferIndex < a_bufferCount; ++bufferIndex)
        {
            sync_render_scene_state(bufferIndex, a_renderWidth, a_renderHeight);
        }

        m_worldResources =
            std::make_unique<WorldResources>(a_bufferManager, a_viewManager);

        Result result = m_worldResources->create_renderable_info_buffer(
            k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_transform_buffer(k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_view_projection_buffer();
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_render_object_buffer(
            k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_object_count_buffer();
        if (!result)
        {
            return result;
        }

        m_ecs.add_system<ECS::RenderableObjectSystem>(
            m_worldResources->renderable_info_uploaders(),
            m_worldResources->transform_uploaders(),
            m_renderSceneState);
        m_ecs.add_system<ECS::CameraSystem>(
            m_worldResources->view_projection_uploaders(), m_renderSceneState);

        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::update(float a_deltaTime, uint32_t a_bufferIndex,
        uint32_t a_renderWidth, uint32_t a_renderHeight)
    {
        execute_deferred_deletions_internal();
        a_deltaTime;
        //animate_static_mesh_objects(a_deltaTime);

        sync_render_scene_state(a_bufferIndex, a_renderWidth, a_renderHeight);

        ECS::UpdateContext updateContext{};
        updateContext.bufferIndex = a_bufferIndex;
        m_ecs.update_all_systems(updateContext);
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_object(
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (m_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }

        Result result = create_object("StaticMeshObject", a_outObject);
        if (!result)
        {
            return result;
        }

        ECS::TransformComponent* transform = nullptr;
        result =
            add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
        if (!result || transform == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add transform component for object.") : result;
        }

        ECS::MeshFilterComponent* meshFilter = nullptr;
        result =
            add_component<ECS::MeshFilterComponent>(a_outObject.entity_id(), meshFilter);
        if (!result || meshFilter == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add mesh filter component for object.") : result;
        }

        ECS::StaticMeshRendererComponent* renderer = nullptr;
        result = add_component<ECS::StaticMeshRendererComponent>(
            a_outObject.entity_id(), renderer);
        if (!result || renderer == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add static mesh renderer component for object.")
                : result;
        }

        transform->position = a_position;
        transform->rotation = Math::float3::zero();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        meshFilter->meshId = m_defaultStaticMeshId;
        renderer->materialId = 0;
        renderer->visible = true;
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_object_to_scene(SceneId a_sceneId,
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }
        if (m_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_static_mesh_object_definition(
                a_position, m_defaultStaticMeshId);
        return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
    }

    [[nodiscard]] Result GameWorld::remove_object(uint32_t a_objectId) noexcept
    {
        EntityId entityId = k_invalidEntityId;
        if (!try_get_static_mesh_entity(a_objectId, entityId))
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Static mesh object id was not found.");
        }

        return destroy_object(entityId);
    }
}
