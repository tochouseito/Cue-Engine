#pragma once

/// **********************************************************************
/// SceneAsset と GameWorld の相互変換を行う
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <string_view>

namespace Cue::GameCore
{
    class GameWorld;
    struct SceneAsset;

    class SceneWorldMapper final
    {
    public:
        /// @brief SceneAsset の authoring data で GameWorld を置き換える
        [[nodiscard]] static Result load_into(
            GameWorld& a_world,
            const SceneAsset& a_scene,
            EntityId& a_outFirstCameraEntity);

        /// @brief GameWorld の authoring data を SceneAsset へ変換する
        [[nodiscard]] static Result make_asset(const GameWorld& a_world, std::string_view a_name,
                                               SceneAsset& a_outScene);
    };
} // namespace Cue::GameCore
