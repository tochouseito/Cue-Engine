#pragma once

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

namespace Cue::ECS
{
    struct ObjectInfoComponent : public IComponentTag
    {
        ObjectInfoComponent() = default;
        ObjectInfoComponent(const ObjectInfoComponent&) = default;
        ObjectInfoComponent& operator=(const ObjectInfoComponent&) = default;
        ObjectInfoComponent(ObjectInfoComponent&&) = default;
        ObjectInfoComponent& operator=(ObjectInfoComponent&&) = default;
        uint32_t objectId = 0;
        uint32_t meshId = 0;
        uint32_t transformId = 0;
        bool visible = true;
    };

    struct TransformComponent : public IComponentTag
    {
        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent& operator=(const TransformComponent&) = default;
        TransformComponent(TransformComponent&&) = default;
        TransformComponent& operator=(TransformComponent&&) = default;
        Math::float3 position = Math::float3::zero();
        Math::float3 rotation = Math::float3::zero();
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };

    struct CameraComponent : public IComponentTag
    {
        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
        CameraComponent& operator=(const CameraComponent&) = default;
        CameraComponent(CameraComponent&&) = default;
        CameraComponent& operator=(CameraComponent&&) = default;
        bool isMain = false; // 描画に使うメインカメラか
        float fovY = 60.0f; // 垂直視野角（度）
        float aspectRatio = 16.0f / 9.0f; // アスペクト比
        float nearZ = 0.1f; // ニアクリップ距離
        float farZ = 1000.0f; // ファークリップ距離
    };
}
