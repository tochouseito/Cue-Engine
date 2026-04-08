#pragma once

// === Base includes ===
#include <CueAssert.h>

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
        explicit ObjectInfoSystem(std::vector<RHI::SlotUploader<GpuData::ObjectInfo>>& a_objectInfoUploaders)
            : ECSManager::System<ObjectInfoComponent>(
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo, const UpdateContext& a_context) {
                    update_component(a_entity, a_objectInfo, a_context);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo, const InitializeContext& a_context) {
                    initialize_component(a_entity, a_objectInfo, a_context);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo, const FinalizeContext& a_context) {
                    finalize_component(a_entity, a_objectInfo, a_context);
                }),
            m_objectInfoUploaders(a_objectInfoUploaders)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentUploader = nullptr;
            if (!m_objectInfoUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_objectInfoUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_objectInfoUploaders.size())
                {
                    m_currentUploader = &m_objectInfoUploaders[uploaderIndex];
                    m_currentUploader->begin_frame();
                }
            }

            ECSManager::System<ObjectInfoComponent>::update(a_context);
            if (m_currentUploader != nullptr && !m_currentUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit object info uploads.");
            }
        }
    private:
        void update_component(Entity a_entity, ObjectInfoComponent& a_objectInfo, const UpdateContext& a_context)
        {
            a_entity;
            a_context;
            GpuData::ObjectInfo gpuObjectInfo{};
            gpuObjectInfo.objectId = a_objectInfo.objectId;
            gpuObjectInfo.visible = a_objectInfo.visible ? 1u : 0u;
            gpuObjectInfo.meshId = a_objectInfo.meshId;
            gpuObjectInfo.transformId = a_objectInfo.transformId;

            if(!m_currentUploader->push(a_objectInfo.objectId, gpuObjectInfo))
            {
                CUE_ASSERTF(false, "Failed to queue object info upload. objectId=%u",
                    a_objectInfo.objectId);
                return;
            }
        }

        void initialize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo, const InitializeContext& a_context)
        {
            a_entity;
            a_objectInfo;
            a_context;
        }

        void finalize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo, const FinalizeContext& a_context)
        {
            a_entity;
            a_objectInfo;
            a_context;
        }

    private:
        std::vector<RHI::SlotUploader<GpuData::ObjectInfo>>& m_objectInfoUploaders;
        RHI::SlotUploader<GpuData::ObjectInfo>* m_currentUploader = nullptr;
    };
} // namespace Cue::ECS
