#pragma once

// === Base includes ===
#include <CueAssert.h>

// === RHI includes ===
#include <RHI.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GpuData/Transform.h>

namespace Cue::ECS
{
    class TransformSystem final
        : public ECSManager::System<ObjectInfoComponent, TransformComponent>
    {
    public:
        explicit TransformSystem(std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>& a_transformUploaders)
            : ECSManager::System<ObjectInfoComponent, TransformComponent>(
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo,
                    TransformComponent& a_transform, const UpdateContext& a_context) {
                    update_component(a_entity, a_objectInfo, a_transform, a_context);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo,
                    TransformComponent& a_transform, const InitializeContext& a_context) {
                    initialize_component(a_entity, a_objectInfo, a_transform, a_context);
                },
                [this](Entity a_entity, ObjectInfoComponent& a_objectInfo,
                    TransformComponent& a_transform, const FinalizeContext& a_context) {
                    finalize_component(a_entity, a_objectInfo, a_transform, a_context);
                }),
            m_transformUploaders(a_transformUploaders)
        {
        }

        void update(const UpdateContext& a_context) override
        {
            m_currentUploader = nullptr;
            if (!m_transformUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_transformUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_transformUploaders.size())
                {
                    m_currentUploader = &m_transformUploaders[uploaderIndex];
                    m_currentUploader->begin_frame();
                }
            }

            ECSManager::System<ObjectInfoComponent, TransformComponent>::update(a_context);
            if (m_currentUploader != nullptr && !m_currentUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit transform uploads.");
            }
        }

    private:
        void update_component(Entity a_entity, ObjectInfoComponent& a_objectInfo,
            TransformComponent& a_transform, const UpdateContext& a_context)
        {
            a_entity;
            a_context;
            GpuData::ObjectTransformGpu gpuTransform{};
            gpuTransform.worldMatrix = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);

            if(!m_currentUploader->push(a_objectInfo.transformId, gpuTransform))
            {
                CUE_ASSERTF(false, "Failed to queue transform upload. transformId=%u",
                    a_objectInfo.transformId);
            }
        }

        void initialize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo,
            TransformComponent& a_transform, const InitializeContext& a_context)
        {
            a_entity;
            a_objectInfo;
            a_transform;
            a_context;
        }

        void finalize_component(Entity a_entity, ObjectInfoComponent& a_objectInfo,
            TransformComponent& a_transform, const FinalizeContext& a_context)
        {
            a_entity;
            a_objectInfo;
            a_transform;
            a_context;
        }

    private:
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>& m_transformUploaders;
        RHI::SlotUploader<GpuData::ObjectTransformGpu>* m_currentUploader = nullptr;
    };
} // namespace Cue::ECS
