#pragma once

// === Base includes ===
#include <CueAssert.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/RenderSceneState.h>

// === C++ includes ===
#include <optional>

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

        void set_buffer_index(uint32_t a_bufferIndex) noexcept
        {
            m_bufferIndex = a_bufferIndex;
        }

        void update() override
        {
            if (m_objectInfoUploaders.empty() || !m_bufferIndex.has_value())
            {
                return;
            }

            const uint32_t uploaderIndex =
                (m_objectInfoUploaders.size() == 1) ? 0u : m_bufferIndex.value();
            if (uploaderIndex >= m_objectInfoUploaders.size())
            {
                return;
            }

            m_currentUploader = &m_objectInfoUploaders[uploaderIndex];
            m_currentUploader->begin_frame();

            ECSManager::System<ObjectInfoComponent>::update();
        }
    private:
        void update_component(Entity a_entity, ObjectInfoComponent& a_objectInfo)
        {
            a_entity;
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

            if (!m_currentUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit object info upload. objectId=%u",
                    a_objectInfo.objectId);
                return;
            }
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
        std::optional<uint32_t> m_bufferIndex;
        RenderSceneState& m_renderSceneState;
        std::vector<RHI::SlotUploader<GpuData::ObjectInfo>>& m_objectInfoUploaders;
        RHI::SlotUploader<GpuData::ObjectInfo>* m_currentUploader = nullptr;
    };
} // namespace Cue::ECS
