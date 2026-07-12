#pragma once

/// **********************************************************************
/// DrawSystem が使用する Camera Entity の選択状態を保持する
/// **********************************************************************

// === Engine includes ===
#include "GameCore/GameCoreTypes.h"

namespace Cue::DrawSystem
{
    /// @brief World ごとに描画 Camera の選択を保持する
    class RenderCameraSelection final
    {
    public:
        /// @brief 描画に使用する Camera Entity を設定する
        void set_camera_entity(GameCore::EntityId a_entityId) noexcept
        {
            m_cameraEntity = a_entityId;
        }

        /// @brief 描画 Camera の選択を解除する
        void clear() noexcept
        {
            m_cameraEntity = GameCore::k_invalidEntityId;
        }

        /// @brief 現在選択されている Camera Entity を取得する
        [[nodiscard]] GameCore::EntityId camera_entity() const noexcept
        {
            return m_cameraEntity;
        }

    private:
        GameCore::EntityId m_cameraEntity = GameCore::k_invalidEntityId;
    };
} // namespace Cue::DrawSystem
