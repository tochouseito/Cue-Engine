#pragma once

/// **********************************************************************
/// シーン、オブジェクトを管理するワールド
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "Components.h"
#include "GameCoreTypes.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Cue::GameCore
{
    struct EntityRecord final
    {
        Generation generation = 0;
        bool isAlive = false;
        // 遅延削除キューへ同じ Entity を二重登録しないためのフラグ
        bool isPendingDestroy = false;
        SceneId sourceSceneId = k_invalidSceneId;
        LocalObjectId sourceLocalObjectId = k_invalidLocalObjectId;
    };

    class GameWorld final
    {
    public:
        GameWorld() = default;
        ~GameWorld() = default;
    private:
        ECS::ECSManager m_ecsManager; // ECSマネージャー
    };
}
