#pragma once

#include <Cue/Math/Transform.h>
#include <Cue/Scene/Identity.h>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
class AssertContext;
}

namespace cue::scene
{
class SceneDocument;

/// @brief SceneDocumentが所有する永続Object Authoring Data
///
/// 返したReference、Pointer、Viewは所有SceneDocumentの次の成功した非const操作、
/// Move、破棄のうち最初の時点まで有効とする。
class SceneObject final
{
  public:
    /// @brief Object Identityを返す
    [[nodiscard]] const ObjectId &id() const noexcept;
    /// @brief 永続Object名を返す
    [[nodiscard]] std::string_view name() const noexcept;
    /// @brief Object自身のActive状態を返す
    [[nodiscard]] bool is_active() const noexcept;
    /// @brief Parent ObjectがあればIdentityへの非所有Pointerを返す
    [[nodiscard]] const ObjectId *try_parent_id() const noexcept;
    /// @brief Core Transform Dataを返す
    [[nodiscard]] const math::Transform &transform() const noexcept;

  private:
    friend class SceneDocument;

    /// @brief 検証済みObject Authoring Dataを構築する
    SceneObject(ObjectId a_id, std::string a_name, bool a_isActive,
                std::optional<ObjectId> a_parentId,
                math::Transform a_transform) noexcept;

    ObjectId m_id;
    std::string m_name;
    bool m_isActive;
    std::optional<ObjectId> m_parentId;
    math::Transform m_transform;
};

/// @brief RuntimeとEditor一時状態を含まないAuthoring Sceneの編集・保存正本
///
/// 生成ThreadだけがMutationを直列実行し、複数Threadから同時に操作しない。
/// 回復可能な公開操作が失敗した場合は、呼び出し前のDocument状態を維持する。
class SceneDocument final
{
  public:
    /// @brief SceneDocumentの一意所有を保つためCopy構築を禁止する
    SceneDocument(const SceneDocument &) = delete;
    /// @brief SceneDocumentの一意所有を保つためCopy代入を禁止する
    SceneDocument &operator=(const SceneDocument &) = delete;
    /// @brief SceneDocument所有Dataを移動する
    SceneDocument(SceneDocument &&) noexcept = default;
    /// @brief SceneDocument所有Dataを移動代入する
    SceneDocument &operator=(SceneDocument &&) noexcept = default;
    /// @brief SceneDocument所有Dataを破棄する
    ~SceneDocument() noexcept;

    /// @brief Scene Identityと非所有診断Contextから空Documentを生成する
    /// @param a_assertContext OwnerがSceneDocumentより長く生存させる診断Context
    [[nodiscard]] static SceneDocument create(
        SceneAssetId a_sceneAssetId,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Rootを1とする許容Hierarchy Depth上限を返す
    [[nodiscard]] static constexpr std::size_t maximum_hierarchy_depth() noexcept
    {
        return 256U;
    }

    /// @brief 永続Scene Identityを返す
    [[nodiscard]] const SceneAssetId &scene_asset_id() const noexcept;
    /// @brief Object数を返す
    [[nodiscard]] std::size_t object_count() const noexcept;
    /// @brief Object集合を次の成功Mutation、Document Move、破棄まで有効な連続Viewで返す
    [[nodiscard]] std::span<const SceneObject> objects() const noexcept;
    /// @brief Object Identityに対応し次の成功Mutation、Document Move、破棄まで有効な非所有Pointerを返す
    [[nodiscard]] const SceneObject *find_object(const ObjectId &a_id) const noexcept;

    /// @brief 検証済みStable IDとAuthoring DataでObjectを追加する
    [[nodiscard]] Result<void> add_object(
        ObjectId a_id, std::string_view a_name, bool a_isActive,
        std::optional<ObjectId> a_parentId,
        math::Transform a_transform) noexcept;
    /// @brief Childを持たないObjectを削除する
    [[nodiscard]] Result<void> remove_object(const ObjectId &a_id) noexcept;
    /// @brief Object名を空でない新しい値へ変更する
    [[nodiscard]] Result<void> rename_object(const ObjectId &a_id,
                                             std::string_view a_name) noexcept;
    /// @brief DanglingまたはCycleを作らないParentへ付け替える
    [[nodiscard]] Result<void> set_parent(
        const ObjectId &a_id,
        std::optional<ObjectId> a_parentId) noexcept;
    /// @brief Object自身のActive状態を変更する
    [[nodiscard]] Result<void> set_active(const ObjectId &a_id,
                                          bool a_isActive) noexcept;
    /// @brief ObjectのCore Transform Dataを置き換える
    [[nodiscard]] Result<void> set_transform(
        const ObjectId &a_id,
        math::Transform a_transform) noexcept;

    /// @brief Stable ID IndexとHierarchy Invariantを再検証する
    [[nodiscard]] Result<void> validate() const noexcept;

  private:
    /// @brief Scene Identityと診断Contextを保持する空Documentを構築する
    SceneDocument(SceneAssetId a_sceneAssetId,
                  const AssertContext &a_assertContext) noexcept;
    /// @brief Identityに対応する可変Objectへの内部Pointerを返す
    [[nodiscard]] SceneObject *find_mutable_object(const ObjectId &a_id) noexcept;
    /// @brief 指定Parent ChainにObject自身が含まれるか判定する
    [[nodiscard]] bool would_create_cycle(
        const ObjectId &a_id,
        const ObjectId &a_parentId) const noexcept;
    /// @brief 指定Parentの下へ追加したObjectのRoot始まりDepthを返す
    [[nodiscard]] std::size_t child_depth(
        const ObjectId &a_parentId) const noexcept;
    /// @brief 指定Objectを1とするSubtree最大相対Depthを反復走査で返す
    [[nodiscard]] std::size_t subtree_height(
        const ObjectId &a_id) const noexcept;
    /// @brief Object配列の現在位置からStable ID Indexを再構築する
    void rebuild_index() noexcept;
    /// @brief 予期しない例外をScene境界のFatal終了へ変換する
    [[noreturn]] void terminate_exception() const noexcept;

    SceneAssetId m_sceneAssetId;
    const AssertContext *m_assertContext;
    std::vector<SceneObject> m_objects;
    std::map<ObjectId, std::size_t> m_objectIndex;
};
} // namespace cue::scene
