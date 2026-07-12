#pragma once

/// **********************************************************************
/// Editor Undo 用の GameObject snapshot を構築・復元する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "GameCoreTypes.h"

namespace Cue::DrawSystem
{
    class RenderCameraSelection;
}

namespace Cue::GameCore
{
    class GameWorld;
    struct ObjectSnapshot;

    class ObjectSnapshotService final
    {
    public:
        /// @brief 指定 Entity の authoring 状態を snapshot として取得する
        [[nodiscard]] static Result capture(
            const GameWorld& a_world,
            const DrawSystem::RenderCameraSelection& a_cameraSelection,
            EntityId a_entityId,
            ObjectSnapshot& a_outSnapshot);

        /// @brief snapshot を元の Entity ID で GameWorld へ復元する
        [[nodiscard]] static Result restore(
            GameWorld& a_world,
            DrawSystem::RenderCameraSelection& a_cameraSelection,
            const ObjectSnapshot& a_snapshot,
            EntityId& a_outEntityId);
    };
} // namespace Cue::GameCore
