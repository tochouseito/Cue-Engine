#pragma once

/// ************************************************************************************
/// GameObject.h
/// *************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue::GameCore
{
    class GameWorld;

    /// @brief GameWorld が返す軽量な GameObject ハンドル。
    ///
    /// EntityId と Generation
    /// を保持し、参照先が同じ世代で生存している場合だけ有効として扱う。
    class GameObject final
    {
        public:
        /// @brief 無効な GameObject ハンドルを生成する。
        GameObject() noexcept;

        /// @brief GameWorld 内の Entity を参照するハンドルを生成する。
        /// @param a_world 参照先の GameWorld。
        /// @param a_entityId 参照先 Entity の ID。
        /// @param a_generation ハンドル生成時点の Entity 世代番号。
        GameObject(GameWorld* a_world, EntityId a_entityId, Generation a_generation) noexcept;

        /// @brief 参照先 Entity が同じ世代で生存しているかを返す。
        [[nodiscard]] bool is_valid() const noexcept;

        /// @brief is_valid() と同じ有効性判定を bool 変換として返す。
        [[nodiscard]] explicit operator bool() const noexcept;

        /// @brief 参照先 Entity の ID を返す。
        [[nodiscard]] EntityId entity_id() const noexcept;

        /// @brief ハンドル生成時点の Entity 世代番号を返す。
        [[nodiscard]] Generation generation() const noexcept;

        /// @brief GameWorld が管理する表示名を取得する。
        /// @param a_outName 取得した表示名の出力先。
        [[nodiscard]] Result name(std::string& a_outName) const;

        /// @brief GameWorld が管理する表示名を変更する。
        /// @param a_name 新しい表示名。
        [[nodiscard]] Result set_name(std::string_view a_name);

        /// @brief GameWorld が管理するタグを取得する。
        /// @param a_outTag 取得したタグの出力先。
        [[nodiscard]] Result tag(std::string& a_outTag) const;

        /// @brief GameWorld が管理するタグを変更する。
        /// @param a_tag 新しいタグ。
        [[nodiscard]] Result set_tag(std::string_view a_tag);

        /// @brief 自身のアクティブ状態を取得する。
        /// @param a_outIsActive アクティブ状態の出力先。
        [[nodiscard]] Result is_active(bool& a_outIsActive) const;

        /// @brief 自身のアクティブ状態を変更する。
        /// @param a_isActive 有効にする場合は true。
        [[nodiscard]] Result set_active(bool a_isActive);

        /// @brief Scene アンロード時に残す永続 Object かを取得する。
        /// @param a_outIsPersistent 永続状態の出力先。
        [[nodiscard]] Result is_persistent(bool& a_outIsPersistent) const;

        /// @brief Scene アンロード時に残す永続 Object かを変更する。
        /// @param a_isPersistent 永続 Object にする場合は true。
        [[nodiscard]] Result set_persistent(bool a_isPersistent);

        /// @brief 指定型の Component を取得する。
        /// @tparam T 取得する Component 型。
        /// @param a_outComponent 取得した Component の非所有ポインタ出力先。
        template <typename T> [[nodiscard]] Result get_component(T*& a_outComponent) noexcept;

        /// @brief 指定型の Component を追加する。
        /// @tparam T 追加する Component 型。
        /// @tparam Args Component 構築引数の型。
        /// @param a_outComponent 追加した Component の非所有ポインタ出力先。
        /// @param a_args Component 構築引数。
        template <typename T, typename... Args>
        [[nodiscard]] Result add_component(T*& a_outComponent, Args&&... a_args);

        /// @brief 指定型の Component を持っているかを取得する。
        /// @tparam T 判定する Component 型。
        /// @param a_outHasComponent Component を持つ場合は true。
        template <typename T> [[nodiscard]] Result has_component(bool& a_outHasComponent) const noexcept;

        /// @brief 指定型の Component を削除する。
        /// @tparam T 削除する Component 型。
        template <typename T> [[nodiscard]] Result remove_component() noexcept;

        /// @brief GameWorld の遅延削除キューへ自身の削除を要求する。
        [[nodiscard]] Result destroy() noexcept;

        private:
        /// @brief 参照先の GameWorld。nullptr の場合は無効ハンドル。
        GameWorld* m_world = nullptr;
        /// @brief 参照先 Entity の ID。
        EntityId m_entityId = k_invalidEntityId;
        /// @brief ハンドル生成時点の Entity 世代番号。
        Generation m_generation = 0;
    };
} // namespace Cue::GameCore
