#pragma once

#include <Cue/EditorCore/EditorController.h>
#include <Cue/Math/Transform.h>
#include <Cue/Scene/ComponentData.h>
#include <Cue/Scene/Identity.h>
#include <Cue/Schema/Registry.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cue
{
class AssertContext;
}

namespace cue::editor
{
/// @brief Add Component UIへ公開する表示名と検証済み初期値Template
struct EditorComponentTemplate final
{
    std::string displayName;
    scene::SceneComponent prototype;
};

/// @brief Stable Object Identity集合へEditor Selectionを切り替えるIntent
struct SelectObjectsIntent final
{
    std::vector<scene::ObjectId> objectIds;
    std::optional<scene::ObjectId> primaryObjectId;
};

/// @brief 指定ParentまたはScene Rootへ初期Objectを追加するIntent
struct AddObjectIntent final
{
    std::optional<scene::ObjectId> parentId;
    std::string name;
};

/// @brief Stable Object IdentityのSubtreeを削除するIntent
struct DeleteObjectIntent final
{
    scene::ObjectId objectId;
};

/// @brief Stable Object IdentityのSubtreeを新しいIdentity群へ複製するIntent
struct DuplicateObjectIntent final
{
    scene::ObjectId objectId;
};

/// @brief Stable Object Identityの永続名を変更するIntent
struct RenameObjectIntent final
{
    scene::ObjectId objectId;
    std::string name;
};

/// @brief Stable Object Identityを別Parentへ移動またはRootへ切り離すIntent
struct ReparentObjectIntent final
{
    scene::ObjectId objectId;
    std::optional<scene::ObjectId> parentId;
};

/// @brief Stable Object IdentityのCore Transformを置き換えるIntent
struct EditTransformIntent final
{
    scene::ObjectId objectId;
    math::Transform transform;
};

/// @brief Template Type Identityから新しいComponent Instanceを追加するIntent
struct AddComponentIntent final
{
    scene::ObjectId objectId;
    schema::TypeId componentTypeId;
};

/// @brief Stable Object／Component IdentityのComponentを削除するIntent
struct RemoveComponentIntent final
{
    scene::ObjectId objectId;
    scene::ComponentInstanceId componentId;
};

/// @brief 現在Documentの直前Transactionを取り消すIntent
struct UndoIntent final
{
};

/// @brief 現在Documentの取り消し済みTransactionを再適用するIntent
struct RedoIntent final
{
};

/// @brief Hierarchy・Inspector最小UIがEditorControllerへ渡す意味Intentの閉じた集合
using EditorIntent = std::variant<SelectObjectsIntent, AddObjectIntent, DeleteObjectIntent, DuplicateObjectIntent,
                                  RenameObjectIntent, ReparentObjectIntent, EditTransformIntent, AddComponentIntent,
                                  RemoveComponentIntent, UndoIntent, RedoIntent>;

/// @brief EditorControllerのRead-only ViewをHierarchy・Inspector操作へ変換するPresentation Adapter
///
/// Controller、Identity Source、Schema Registry、Assert ContextはPresenterより長く生存させ、
/// 生成Threadだけでdrawとsubmitを呼び出す。Component Template群はPresenterが所有する
class EditorPresenter final
{
  public:
    /// @brief Presentation Stateと注入参照の一意性を保つためCopy構築を禁止する
    EditorPresenter(const EditorPresenter &) = delete;
    /// @brief Presentation Stateと注入参照の一意性を保つためCopy代入を禁止する
    EditorPresenter &operator=(const EditorPresenter &) = delete;
    /// @brief ImGui Session中のAddress安定性を保つためMove構築を禁止する
    EditorPresenter(EditorPresenter &&) = delete;
    /// @brief ImGui Session中のAddress安定性を保つためMove代入を禁止する
    EditorPresenter &operator=(EditorPresenter &&) = delete;
    /// @brief Presentation Stateを破棄し、注入Objectの所有権は変更しない
    ~EditorPresenter() = default;

    /// @brief 一つのOpen Documentへ結び付くHierarchy・Inspector Adapterを生成する
    /// @param a_componentTemplates Presenterが所有権を受け取りAdd Component候補として保持する
    [[nodiscard]] static std::unique_ptr<EditorPresenter> create(
        editor_core::EditorController &a_controller, editor_core::EditorDocumentId a_documentId,
        scene::SceneIdentitySource &a_identitySource, const schema::SchemaRegistry &a_schemaRegistry,
        std::vector<EditorComponentTemplate> a_componentTemplates, const AssertContext &a_assertContext) noexcept;

    /// @brief 現在DocumentのRead-only ViewからHierarchy・Inspectorを描画し、Frame末尾で最大一Intentを適用する
    void draw() noexcept;

    /// @brief 意味IntentをEditorControllerのSelection操作またはSceneCommandへ変換する
    /// @details 失敗時はAuthoring SceneをControllerのRollback規則に従って維持し、Errorと日本語診断を返す
    [[nodiscard]] Result<void> submit(EditorIntent a_intent) noexcept;

    /// @brief 最後の操作結果または診断Messageを返す
    [[nodiscard]] std::string_view message() const noexcept;
    /// @brief 現在Messageが回復可能な操作失敗を表すか返す
    [[nodiscard]] bool has_error_message() const noexcept;

  private:
    /// @brief 注入依存と初期Presentation Stateを保持する
    EditorPresenter(editor_core::EditorController &a_controller, editor_core::EditorDocumentId a_documentId,
                    scene::SceneIdentitySource &a_identitySource, const schema::SchemaRegistry &a_schemaRegistry,
                    std::vector<EditorComponentTemplate> a_componentTemplates,
                    const AssertContext &a_assertContext) noexcept;

    /// @brief Undo／Redo MenuとShortcutを描画して意味Intentを予約する
    void draw_menu(const editor_core::EditorDocument &a_document, std::optional<EditorIntent> &a_pendingIntent);
    /// @brief Stable ObjectIdをImGui IDに使用してHierarchy TreeとObject操作を描画する
    void draw_hierarchy(const editor_core::EditorDocument &a_document, std::optional<EditorIntent> &a_pendingIntent);
    /// @brief 一ObjectとChild群をRead-only Scene Viewから再帰描画する
    void draw_object_node(const scene::SceneDocument &a_sceneDocument, const scene::SceneObject &a_object,
                          std::span<const scene::ObjectId> a_selection, std::optional<EditorIntent> &a_pendingIntent);
    /// @brief Primary SelectionのName、Parent、Transform、Component操作を描画する
    void draw_inspector(const editor_core::EditorDocument &a_document, std::optional<EditorIntent> &a_pendingIntent);
    /// @brief SelectionまたはDocument State変更時に編集BufferをRead-only正本へ同期する
    void sync_inspector(const editor_core::EditorDocument &a_document, const scene::SceneObject &a_object);
    /// @brief Error分類と開発者診断を日本語の回復可能Messageへ変換する
    void set_error(const Error &a_error) noexcept;
    /// @brief 成功した操作の短いMessageを設定する
    void set_status(std::string_view a_status) noexcept;

    editor_core::EditorController *m_controller;
    scene::SceneIdentitySource *m_identitySource;
    const schema::SchemaRegistry *m_schemaRegistry;
    const AssertContext *m_assertContext;
    std::vector<EditorComponentTemplate> m_componentTemplates;
    editor_core::EditorDocumentId m_documentId;
    std::optional<scene::ObjectId> m_inspectorObjectId;
    std::uint64_t m_inspectorStateValue = 0U;
    std::string m_name;
    std::array<float, 3> m_translation{};
    std::array<float, 4> m_rotation{};
    std::array<float, 3> m_scale{1.0F, 1.0F, 1.0F};
    std::string m_message;
    bool m_hasError = false;
};
} // namespace cue::editor
