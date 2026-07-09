#pragma once

/// ************************************************************************************
/// Runtime と Editor が共有する Scene asset の最小データ構造
/// ************************************************************************************

// === Math includes ===
#include <CueMath.h>

// === Engine includes ===
#include "Components.h"
#include "GameCoreTypes.h"

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

namespace Cue::GameCore
{
    struct SceneTransform final
    {
        Math::Quaternion rotation = Math::Quaternion::identity();
        Math::float3 position = Math::float3::zero();
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };

    struct SceneCamera final
    {
        float fovY = 60.0f;
        float aspectRatio = 0.0f;
        float nearZ = 0.1f;
        float farZ = 1000.0f;
    };

    struct SceneRenderable final
    {
        ECS::MaterialPropertyBlock propertyBlock{};
        std::string modelName{};
        std::uint32_t meshId = ECS::k_invalidMeshId;
        std::uint32_t materialId = ECS::k_invalidMaterialId;
        ECS::RenderQueue renderQueue = ECS::RenderQueue::Auto;
        ECS::ShadowCasterMode shadowCasterMode = ECS::ShadowCasterMode::Solid;
        bool visible = true;
        bool castsShadow = true;
        bool receivesShadow = true;
    };

    struct SceneObject final
    {
        std::string name{};
        std::string tag{"Default"};
        LocalObjectId localId = k_invalidLocalObjectId;
        LocalObjectId parentLocalId = k_invalidLocalObjectId;
        SceneTransform transform{};
        SceneCamera camera{};
        SceneRenderable renderable{};
        bool isActive = true;
        bool isPersistent = false;
        bool hasTransform = false;
        bool hasCamera = false;
        bool hasRenderable = false;
    };

    struct SceneAsset final
    {
        std::string name{};
        std::vector<SceneObject> objects{};
        std::uint32_t version = 1;
    };
} // namespace Cue::GameCore
