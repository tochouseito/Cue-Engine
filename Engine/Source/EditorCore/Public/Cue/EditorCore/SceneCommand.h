#pragma once

#include <Cue/EditorCore/EditorDocument.h>
#include <Cue/Math/Transform.h>
#include <Cue/Scene/ComponentData.h>
#include <Cue/Scene/Identity.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cue::editor_core
{
/// @brief Stable Identity と初期 Authoring Data で Object を追加する Command
struct AddObjectCommand final
{
    scene::ObjectId objectId;
    std::string name;
    bool isActive;
    std::optional<scene::ObjectId> parentId;
    math::Transform transform;
};

/// @brief Stable Object Identity の Subtree 全体を削除する Command
struct DeleteObjectCommand final
{
    scene::ObjectId objectId;
};

/// @brief Duplicate 元と新しい Object／Component Identity の対応を保持する
/// @details duplicateComponentIds は Source Object の components() と同じ順序・要素数で指定する
struct DuplicateObjectTarget final
{
    scene::ObjectId sourceObjectId;
    scene::ObjectId duplicateObjectId;
    std::string name;
    std::vector<scene::ComponentInstanceId> duplicateComponentIds;
};

/// @brief Object Subtree と既知 Component 値を指定済み Stable Identity へ複製する Command
/// @details targets は Source Root を含む Subtree 全体を過不足なく一度ずつ指定する
/// Opaque Component は保存 Entry 内 Identity の Lossless 変更が未定義のため拒否する
struct DuplicateObjectCommand final
{
    scene::ObjectId sourceRootId;
    std::vector<DuplicateObjectTarget> targets;
};

/// @brief Stable Object Identity の永続 Name を変更する Command
struct RenameObjectCommand final
{
    scene::ObjectId objectId;
    std::string name;
};

/// @brief Stable Object Identity を同じ Document 内の Parent へ移動または Root へ切り離す Command
struct ReparentObjectCommand final
{
    scene::ObjectId objectId;
    std::optional<scene::ObjectId> parentId;
};

/// @brief Stable Object Identity へ検証済み Component Data を追加する Command
struct AddComponentCommand final
{
    scene::ObjectId objectId;
    scene::SceneComponent component;
};

/// @brief Stable Object／Component Identity の Component を削除する Command
struct RemoveComponentCommand final
{
    scene::ObjectId objectId;
    scene::ComponentInstanceId componentId;
};

/// @brief Stable Object／Component／Field Identity の既知 Field Value を変更する Command
struct EditFieldCommand final
{
    scene::ObjectId objectId;
    scene::ComponentInstanceId componentId;
    schema::FieldId fieldId;
    scene::FieldValue value;
};

/// @brief Stable Object Identity の Core Transform を変更する Command
struct EditTransformCommand final
{
    scene::ObjectId objectId;
    math::Transform transform;
};

/// @brief M12 Scene 編集で許可する型付き Command Request の閉じた集合
using SceneEditCommand = std::variant<AddObjectCommand, DeleteObjectCommand, DuplicateObjectCommand,
                                      RenameObjectCommand, ReparentObjectCommand, AddComponentCommand,
                                      RemoveComponentCommand, EditFieldCommand, EditTransformCommand>;

/// @brief Command の対象 Editor Document と永続 Scene を二重に照合する公開 Request
struct SceneCommandRequest final
{
    EditorDocumentId documentId;
    scene::SceneAssetId sceneAssetId;
    SceneEditCommand command;
};

/// @brief 一つの利用者操作としてAtomicに適用するScene Command集合
struct EditorTransaction final
{
    std::string label;
    std::vector<SceneCommandRequest> commands;
};
} // namespace cue::editor_core
