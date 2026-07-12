#pragma once

/// **********************************************************************
/// GameObject を Undo / Redo で復元するための authoring 状態を定義する
/// **********************************************************************

// === Engine includes ===
#include "Components.h"
#include "GameCoreTypes.h"

// === C++ includes ===
#include <string>

namespace Cue::GameCore
{
    /// @brief 削除前 GameObject を同じ Entity ID で再構築するための状態
    struct ObjectSnapshot final
    {
        std::string name{};
        std::string tag{};
        ECS::TransformComponent transform{};
        ECS::CameraComponent camera{};
        ECS::MeshFilterComponent meshFilter{};
        ECS::StaticMeshRendererComponent staticMeshRenderer{};
        ECS::ScriptComponent script{};
        EntityId entityId = k_invalidEntityId;
        EntityId parentId = k_invalidEntityId;
        SceneId owningSceneId = k_invalidSceneId;
        SceneId sourceSceneId = k_invalidSceneId;
        LocalObjectId sourceLocalObjectId = k_invalidLocalObjectId;
        bool isActive = true;
        bool isPersistent = false;
        bool isRenderCamera = false;
        bool hasCamera = false;
        bool hasMeshFilter = false;
        bool hasStaticMeshRenderer = false;
        bool hasScript = false;
    };
} // namespace Cue::GameCore
