#pragma once

/// ************************************************************************************
/// GameObjectProto.h
/// *************************************************************************************

// === ECS includes ===
#include "ECSManager.h"

// === C++ includes ===
#include <string>

namespace Cue::GameCore
{
    /// @brief GameObject 生成時に使う雛形コンポーネント群と基本属性を保持する。
    ///
    /// ECS::Prototype が保持する Component 群に加えて、GameWorld
    /// 側で使う表示名とタグを保持する。
    class GameObjectProto final : public ECS::Prototype
    {
    public:
        /// @brief 空の名前と既定タグを持つ Prototype を生成する。
        GameObjectProto();

        /// @brief 表示名とタグを指定して Prototype を生成する。
        /// @param name GameObject 生成時に設定する表示名。
        /// @param tag GameObject 生成時に設定するタグ。
        GameObjectProto(std::string name, std::string tag);

        /// @brief Prototype が保持する Component 群を破棄する。
        ~GameObjectProto();

        /// @brief GameObject 生成時に設定する表示名を返す。
        const std::string& name() const;

        /// @brief GameObject 生成時に設定するタグを返す。
        const std::string& tag() const;

        /// @brief 指定 Component を Prototype に追加する。
        /// @tparam T 追加する Component 型。
        /// @param a_comp 追加する Component 値。
        template <ECS::ComponentType T> void add_component(const T& a_comp);

        /// @brief 指定 Component を Prototype 上で置き換える。
        /// @tparam T 置き換える Component 型。
        /// @param a_comp 新しい Component 値。
        template <ECS::ComponentType T> void set_component(const T& a_comp);

        /// @brief Prototype に保持した Component 群を指定 Entity へ復元する。
        /// @param a_entity 復元先 Entity。
        /// @param a_ecs 復元先 Entity を管理する ECSManager。
        void restore_components_into(ECS::Entity a_entity, ECS::ECSManager& a_ecs) const;

        /// @brief 既存 Entity の Component 群と基本属性から Prototype を生成する。
        /// @param a_ecs 参照元 Entity を管理する ECSManager。
        /// @param a_e 参照元 Entity。
        /// @param a_name 生成する Prototype の表示名。
        /// @param a_tag 生成する Prototype のタグ。
        static GameObjectProto from_entity(ECS::ECSManager& a_ecs, ECS::Entity a_e, const std::string& a_name,
                                           const std::string& a_tag = "Default");

    private:
        /// @brief Prototype 復元に必要な Component コピー関数を登録する。
        /// @tparam T 登録する Component 型。
        template <ECS::ComponentType T> static void register_component_type();

        /// @brief ECS::Prototype の Entity 生成処理を使用する。
        /// @param a_ecs Entity 生成先の ECSManager。
        ECS::Entity create_entity(ECS::ECSManager& a_ecs) const override;

        /// @brief GameObject 生成時に設定する表示名。
        std::string m_name;
        /// @brief GameObject 生成時に設定するタグ。
        std::string m_tag;
    };
} // namespace Cue::GameCore
