#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/RenderSceneState.h>

namespace Cue::ECS
{
    class ObjectInfoSystem final : public ECSManager::System<ObjectInfoComponent>
    {
    public:
        explicit ObjectInfoSystem(RenderSceneState& a_renderSceneState, std::vector<RHI::SlotUploader<GpuData::ObjectInfo>>& a_objectInfoUploaders)
            : ECSManager::System<ObjectInfoComponent>(
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo) {
                    update_component(a_entity, a_objectInfo);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo) {
                    initialize_component(a_entity, a_objectInfo);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo) {
                    finalize_component(a_entity, a_objectInfo);
                }),
            m_renderSceneState(a_renderSceneState), m_objectInfoUploaders(a_objectInfoUploaders)
        {}


    private:
        void update_component(Entity a_entity, ObjectInfoComponent& a_objectInfo)
        {
            a_entity;
            if (a_objectInfo.objectId >= m_renderSceneState.objectInfos.size())
            {
                return;
            }

            GpuData::ObjectInfo& gpuObjectInfo =
                m_renderSceneState.objectInfos[a_objectInfo.objectId];
            gpuObjectInfo.objectId = a_objectInfo.objectId;
            gpuObjectInfo.visible = a_objectInfo.visible ? 1u : 0u;
            gpuObjectInfo.meshId = a_objectInfo.meshId;
            gpuObjectInfo.transformId = a_objectInfo.transformId;
        }

        void initialize_component(Entity a_entity,
            ObjectInfoComponent& a_objectInfo)
        {
            update_component(a_entity, a_objectInfo);
        }

        void finalize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo)
        {
            a_entity;
            a_objectInfo;
        }

    private:
        RenderSceneState& m_renderSceneState;
        std::vector<RHI::SlotUploader<GpuData::ObjectInfo>>& m_objectInfoUploaders;
    };
} // namespace Cue::ECS
