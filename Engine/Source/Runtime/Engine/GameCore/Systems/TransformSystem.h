#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/RenderSceneState.h>

namespace Cue::ECS
{
    class TransformSystem final
        : public ECSManager::System<ObjectInfoComponent, TransformComponent>
    {
    public:
        explicit TransformSystem(RenderSceneState& a_renderSceneState)
            : ECSManager::System<ObjectInfoComponent, TransformComponent>(
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo,
                    TransformComponent& a_transform) {
                        update_component(a_entity, a_objectInfo, a_transform);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo,
                    TransformComponent& a_transform) {
                        initialize_component(a_entity, a_objectInfo, a_transform);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo,
                    TransformComponent& a_transform) {
                        finalize_component(a_entity, a_objectInfo, a_transform);
                }),
            m_renderSceneState(a_renderSceneState)
        {}

    private:
        void update_component(Entity a_entity, ObjectInfoComponent& a_objectInfo,
            TransformComponent& a_transform)
        {
            a_entity;
            if (a_objectInfo.transformId >= m_renderSceneState.localTransforms.size())
            {
                return;
            }

            GpuData::LocalTransform& localTransform =
                m_renderSceneState.localTransforms[a_objectInfo.transformId];
            localTransform.position = a_transform.position;
            localTransform.rotation = a_transform.rotation;
            localTransform.scale = a_transform.scale;
        }

        void initialize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo,
            TransformComponent& a_transform)
        {
            update_component(a_entity, a_objectInfo, a_transform);
        }

        void finalize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo,
            TransformComponent& a_transform)
        {
            a_entity;
            a_objectInfo;
            a_transform;
        }

    private:
        RenderSceneState& m_renderSceneState;
    };
} // namespace Cue::ECS
