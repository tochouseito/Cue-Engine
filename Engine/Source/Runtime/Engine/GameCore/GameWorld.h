#pragma once

/// **********************************************************************
/// シーン、オブジェクトを管理する最小 World
/// **********************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "Components.h"
#include "GameCoreTypes.h"
#include "GameObject.h"
#include "GameObjectProto.h"

// === C++ includes ===
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cue::GameCore
{
    struct EntityRecord final
    {
        // 古い GameObject ハンドルを無効化するための世代番号
        Generation generation = 0;
        // Entity が GameWorld 上で生存しているか
        bool isAlive = false;
        // 遅延削除キューへ積まれているか
        bool isPendingDestroy = false;
        // 生成元 Scene。Scene 未所属なら k_invalidSceneId
        SceneId sourceSceneId = k_invalidSceneId;
        // 生成元 Scene 内のローカル ID。Scene 未所属なら k_invalidLocalObjectId
        LocalObjectId sourceLocalObjectId = k_invalidLocalObjectId;
    };

    class GameWorld final
    {
        public:
        /// @brief 空の GameWorld を生成する
        GameWorld() noexcept;

        /// @brief GameWorld が所有する ECS 状態を破棄する
        ~GameWorld();

        /// @brief 内部 ECSManager への非所有ポインタを取得する
        [[nodiscard]] Result ecs(ECS::ECSManager*& a_outEcs) noexcept;

        /// @brief 既定タグを持つ空の GameObject を生成する
        [[nodiscard]] Result create_object(std::string_view a_name, GameObject& a_outObject);

        /// @brief 指定名とタグを持つ空の GameObject を生成する
        [[nodiscard]] Result create_object(std::string_view a_name, std::string_view a_tag, GameObject& a_outObject);

        /// @brief 既定名と既定位置で StaticMesh GameObject を生成する
        [[nodiscard]] Result create_static_mesh_object(uint32_t a_meshId, uint32_t a_materialId,
                                                       GameObject& a_outObject);

        /// @brief 指定名と位置で StaticMesh GameObject を生成する
        [[nodiscard]] Result create_static_mesh_object(std::string_view a_name, uint32_t a_meshId,
                                                       uint32_t a_materialId, const Math::float3& a_position,
                                                       GameObject& a_outObject);

        /// @brief GameObjectProto の component 群から GameObject を生成する
        [[nodiscard]] Result instantiate_object(const GameObjectProto& a_proto, GameObject& a_outObject);

        /// @brief GameObject の遅延削除を要求する
        [[nodiscard]] Result destroy_object(EntityId a_entityId) noexcept;

        /// @brief 遅延削除キューに積まれた GameObject を実際に破棄する
        void execute_deferred_deletions() noexcept;

        /// @brief GameWorld 内の全 GameObject を破棄する
        [[nodiscard]] Result clear() noexcept;

        /// @brief 現在生存している GameObject 数を取得する
        [[nodiscard]] Result object_count(size_t& a_outCount) const noexcept;

        /// @brief EntityId と世代番号が現在も有効かを返す
        [[nodiscard]] Result is_alive(EntityId a_entityId, Generation a_generation, bool& a_outIsAlive) const noexcept;

        /// @brief GameObject の表示名を取得する
        [[nodiscard]] Result get_object_name(EntityId a_entityId, std::string& a_outName) const;

        /// @brief GameObject の表示名を変更する
        [[nodiscard]] Result set_object_name(EntityId a_entityId, std::string_view a_name);

        /// @brief GameObject のタグを取得する
        [[nodiscard]] Result get_object_tag(EntityId a_entityId, std::string& a_outTag) const;

        /// @brief GameObject のタグを変更する
        [[nodiscard]] Result set_object_tag(EntityId a_entityId, std::string_view a_tag);

        /// @brief GameObject 自身のアクティブ状態を取得する
        [[nodiscard]] Result is_object_active(EntityId a_entityId, bool& a_outIsActive) const;

        /// @brief GameObject 自身のアクティブ状態を変更する
        [[nodiscard]] Result set_object_active(EntityId a_entityId, bool a_isActive);

        /// @brief GameObject が永続 Object として扱われるかを取得する
        [[nodiscard]] Result is_object_persistent(EntityId a_entityId, bool& a_outIsPersistent) const;

        /// @brief GameObject の永続 Object フラグを変更する
        [[nodiscard]] Result set_object_persistent(EntityId a_entityId, bool a_isPersistent);

        /// @brief 指定タグの GameObject を列挙する
        [[nodiscard]] Result find_objects_by_tag(std::string_view a_tag, std::vector<GameObject>& a_outObjects);

        /// @brief 指定名の GameObject を列挙する
        [[nodiscard]] Result find_objects_by_name(std::string_view a_name, std::vector<GameObject>& a_outObjects);

        /// @brief 指定名の最初の GameObject を取得する
        [[nodiscard]] Result find_object_by_name(std::string_view a_name, GameObject& a_outObject);

        /// @brief 生存中の GameObject を順に visitor へ渡す
        [[nodiscard]] Result for_each_object(const std::function<void(EntityId, GameObject)>& a_func);

        /// @brief Entity に紐付く Component を取得する
        template <typename T> [[nodiscard]] Result get_component(EntityId a_entityId, T*& a_outComponent) noexcept;

        /// @brief Entity に Component を追加する
        template <typename T, typename... Args>
        [[nodiscard]] Result add_component(EntityId a_entityId, T*& a_outComponent, Args&&... a_args);

        /// @brief Entity が Component を持つかを取得する
        template <typename T>
        [[nodiscard]] Result has_component(EntityId a_entityId, bool& a_outHasComponent) const noexcept;

        /// @brief Entity から Component を削除する
        template <typename T> [[nodiscard]] Result remove_component(EntityId a_entityId) noexcept;

        private:
        /// @brief 例外を Result に変換して処理を実行する
        [[nodiscard]] static Result capture_result(const std::function<void()>& a_func);

        /// @brief ECS Entity と GameWorld 管理レコードを生成する
        [[nodiscard]] EntityId create_entity_record(SceneId a_sourceSceneId, LocalObjectId a_localObjectId);

        /// @brief 全 GameObject 共通の Base/RenderableInfo component を初期化する
        void initialize_base_component(EntityId a_entityId, std::string_view a_name, std::string_view a_tag,
                                       SceneId a_owningSceneId, EntityId a_parent, bool a_isActive,
                                       bool a_isPersistent);

        /// @brief 指定 Entity を即座に破棄する
        void destroy_object_immediately(EntityId a_entityId) noexcept;

        /// @brief Entity から GameObject ハンドルを生成する
        [[nodiscard]] GameObject make_handle(EntityId a_entityId) noexcept;

        /// @brief Entity が GameWorld 上で生存中かを返す
        [[nodiscard]] bool contains_object(EntityId a_entityId) const noexcept;

        /// @brief EntityRecord を取得する
        [[nodiscard]] EntityRecord* try_get_entity_record(EntityId a_entityId) noexcept;

        /// @brief EntityRecord を取得する
        [[nodiscard]] const EntityRecord* try_get_entity_record(EntityId a_entityId) const noexcept;

        /// @brief 空名を既定名へ正規化する
        [[nodiscard]] std::string normalize_object_name(std::string_view a_name) const;

        /// @brief 指定名が他の生存 Object に使われているかを返す
        [[nodiscard]] bool is_name_taken(std::string_view a_name, EntityId a_ignoredEntityId = k_invalidEntityId) const;

        /// @brief 重複しない Object 名を生成する
        [[nodiscard]] std::string make_unique_object_name(std::string_view a_requestedName,
                                                          EntityId a_ignoredEntityId = k_invalidEntityId) const;

        /// @brief tag index へ Entity を登録する
        void add_object_to_tag_index(EntityId a_entityId, const std::string& a_tag);

        /// @brief name index へ Entity を登録する
        void add_object_to_name_index(EntityId a_entityId, const std::string& a_name);

        /// @brief tag index から Entity を削除する
        void remove_object_from_tag_index(EntityId a_entityId, const std::string& a_tag);

        /// @brief name index から Entity を削除する
        void remove_object_from_name_index(EntityId a_entityId, const std::string& a_name);

        // Entity と Component の実体を保持する ECS
        ECS::ECSManager m_ecsManager{};
        // Object 名から Entity を逆引きする索引
        std::unordered_map<std::string, std::unordered_set<EntityId>> m_nameIndex{};
        // Object タグから Entity を逆引きする索引
        std::unordered_map<std::string, std::unordered_set<EntityId>> m_tagIndex{};
        // EntityId ごとの生存状態と世代情報
        std::vector<EntityRecord> m_entityRecords{};
        // 公開 API から要求された遅延削除キュー
        std::vector<EntityId> m_pendingDestroyedEntities{};
        // 現在生存している Object 数
        size_t m_liveObjectCount = 0;
    };
} // namespace Cue::GameCore
