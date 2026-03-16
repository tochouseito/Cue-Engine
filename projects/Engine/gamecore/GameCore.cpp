#include "GameCore.h"

namespace Cue
{
    Result GameCore::initialize(GraphicsCore::TransformBufferPool& transformBufferPool)
    {
        // 1) transform pool を保持し、描画対象 cube の slot を先に確保して以後の更新先を固定する。
        m_transformBufferPool = &transformBufferPool;
        Result result = m_transformBufferPool->allocate_slot(m_frontCubeTransformSlot);
        if (!result)
        {
            return result;
        }
        result = m_transformBufferPool->allocate_slot(m_backCubeTransformSlot);
        if (!result)
        {
            return result;
        }

        // 2) cube の基準 transform を初期化し、以後は毎フレーム回転だけ更新する。
        m_cubeTransform.position = Math::float3::zero();
        m_cubeTransform.rotation = Math::float3::zero();
        m_cubeTransform.scale = Math::float3(1.75f, 1.75f, 1.75f);

        // 3) 初回 render が未初期化データを読まないよう、全 frame index 分を先に埋める。
        for (uint32_t bufferIndex = 0; bufferIndex < m_transformBufferPool->buffering_count(); ++bufferIndex)
        {
            update(0, bufferIndex);
        }

        return Result::ok();
    }

    void GameCore::update(uint64_t frameNo, uint32_t index)
    {
        // 1) pool 未初期化時は更新先が無いので何もしない。
        if (m_transformBufferPool == nullptr)
        {
            return;
        }

        // 2) 基準 cube transform を更新し、update thread が毎フレーム回転だけを進める責務を持つ。
        const float rotationRadians = static_cast<float>(frameNo) * 0.01f;
        m_cubeTransform.rotation = Math::float3(rotationRadians, 0.0f, 0.0f);

        // 3) 対象 frame index の upload queue を開き、同フレームの transform 更新を 1 回の commit に集約する。
        Result result = m_transformBufferPool->begin_frame(index);
        if (!result)
        {
            return;
        }

        Core::Native::ObjectTransformGpu frontCubeTransform{};
        frontCubeTransform.worldMatrix = Math::make_affine_matrix(
            m_cubeTransform.scale,
            m_cubeTransform.rotation,
            m_cubeTransform.position);
        result = m_transformBufferPool->push(index, m_frontCubeTransformSlot, frontCubeTransform);
        if (!result)
        {
            return;
        }

        // 4) 奥 cube は同じ回転を共有しつつ、奥行きと縮尺だけを変えて別 slot へ積む。
        Core::Native::LocalTransform backCubeTransform = m_cubeTransform;
        backCubeTransform.position.m_z += 2.0f;
        backCubeTransform.scale = Math::float3::one();

        Core::Native::ObjectTransformGpu backCubeTransformGpu{};
        backCubeTransformGpu.worldMatrix = Math::make_affine_matrix(
            backCubeTransform.scale,
            backCubeTransform.rotation,
            backCubeTransform.position);
        result = m_transformBufferPool->push(index, m_backCubeTransformSlot, backCubeTransformGpu);
        if (!result)
        {
            return;
        }

        // 5) SlotUploader へ積んだ slot 群を upload buffer に反映し、render 側は copy source を読むだけにする。
        (void)m_transformBufferPool->commit(index);
    }

    GraphicsCore::TransformSlotHandle GameCore::front_cube_transform_slot() const noexcept
    {
        // 1) 手前 cube の描画側は GameCore が確保した slot を参照する。
        return m_frontCubeTransformSlot;
    }

    GraphicsCore::TransformSlotHandle GameCore::back_cube_transform_slot() const noexcept
    {
        // 1) 奥 cube の描画側は GameCore が確保した slot を参照する。
        return m_backCubeTransformSlot;
    }
}
