// SceneInstance の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <unordered_map>
#include <vector>

namespace Cue::GameCore
{
    class SceneAsset;

    struct SceneInstance final
    {
        SceneId sceneId = k_invalidSceneId;
        const SceneAsset* asset = nullptr;
        std::vector<EntityId> entities{};
        std::unordered_map<LocalObjectId, EntityId> localObjectToEntity{};
        bool isLoaded = false;
        bool isActive = true;
        // 遅延アンロード待ちの Scene を二重にキュー登録しないためのフラグ
        bool isPendingUnload = false;
        LocalObjectId nextLocalObjectId = 1;
    };
}
