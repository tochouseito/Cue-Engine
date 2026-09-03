#pragma once

#include <Cue/EditorCore/EditorDocument.h>
#include <Cue/Foundation/Result.h>
#include <Cue/Project/Descriptor.h>

#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace cue
{
class AssertContext;
}

namespace cue::editor_core
{
/// @brief 一つの Project Descriptor と開いた EditorDocument 群の一意 Owner
class ProjectWorkspaceSession final
{
  public:
    ProjectWorkspaceSession(const ProjectWorkspaceSession &) = delete;
    ProjectWorkspaceSession &operator=(const ProjectWorkspaceSession &) = delete;
    ProjectWorkspaceSession(ProjectWorkspaceSession &&) noexcept = default;
    ProjectWorkspaceSession &operator=(ProjectWorkspaceSession &&) noexcept = default;
    ~ProjectWorkspaceSession() noexcept = default;

    /// @brief Session 破棄まで有効な Project Descriptor を返す
    [[nodiscard]] const ProjectDescriptor &project_descriptor() const noexcept;
    /// @brief 次の Controller Mutation または Controller 破棄まで有効な Document View を返す
    [[nodiscard]] std::span<const EditorDocument> documents() const noexcept;
    /// @brief Identity に対応し次の Controller Mutation または破棄まで有効な Read-only Document を返す
    [[nodiscard]] const EditorDocument *find_document(EditorDocumentId a_id) const noexcept;

  private:
    friend class EditorController;

    /// @brief 検証済み Project Descriptor の所有権を Session へ移す
    explicit ProjectWorkspaceSession(ProjectDescriptor &&a_descriptor) noexcept;

    ProjectDescriptor m_descriptor;
    std::vector<EditorDocument> m_documents;
};

/// @brief Editor Intent を UI 非依存 Document 状態遷移へ変換する Owner Thread Controller
///
/// Controller、返した Session View、Document View は作成 Thread だけで使用する
class EditorController final
{
  private:
    /// @brief make_unique 経由の Factory 構築だけを許可する非公開 Key
    struct ConstructionKey final
    {
      private:
        friend class EditorController;

        /// @brief EditorController Factory だけが生成できる構築 Key を作る
        ConstructionKey() noexcept
        {
        }
    };

  public:
    EditorController(const EditorController &) = delete;
    EditorController &operator=(const EditorController &) = delete;
    EditorController(EditorController &&) = delete;
    EditorController &operator=(EditorController &&) = delete;
    ~EditorController() noexcept;

    /// @brief Project Descriptor の所有権を受け取って空 Workspace Session を生成する
    /// @param a_assertContext Controller より長く生存させる診断 Context
    [[nodiscard]] static std::unique_ptr<EditorController> create(ProjectDescriptor &&a_descriptor,
                                                                  const AssertContext &a_assertContext) noexcept;

    /// @brief Factory Passkey と Project Descriptor から Controller を構築する
    EditorController(ConstructionKey, ProjectDescriptor &&a_descriptor, const AssertContext &a_assertContext);

    /// @brief Controller 破棄まで有効な Project Workspace Session の Read-only View を返す
    [[nodiscard]] const ProjectWorkspaceSession &session() const noexcept;

    /// @brief 検証済み SceneDocument を一意な Scene と Locator で開く
    [[nodiscard]] Result<EditorDocumentId> open_document(scene::SceneDocument &&a_document, RelativePath &&a_locator,
                                                         bool a_hasSavedDestination) noexcept;
    /// @brief Stable ObjectId だけを保持し、存在しない ID と重複を安全に除く
    [[nodiscard]] Result<void> set_selection(EditorDocumentId a_documentId,
                                             std::span<const scene::ObjectId> a_objectIds,
                                             const scene::ObjectId *a_primaryObjectId = nullptr) noexcept;
    /// @brief Selection と Primary Selection を空にする
    [[nodiscard]] Result<void> clear_selection(EditorDocumentId a_documentId) noexcept;
    /// @brief 現在 Scene に存在しない選択 ID を除去する
    [[nodiscard]] Result<void> reconcile_selection(EditorDocumentId a_documentId) noexcept;
    /// @brief 成功した永続 Data 変更に一つの新しい State Identity を発行する
    [[nodiscard]] Result<DocumentStateId> record_persistent_change(EditorDocumentId a_documentId) noexcept;
    /// @brief 確定保存された発行済み State を保存地点として記録する
    [[nodiscard]] Result<void> mark_saved(EditorDocumentId a_documentId, DocumentStateId a_savedStateId) noexcept;
    /// @brief Scene 正本に対する外部変更の観測状態を更新する
    [[nodiscard]] Result<void> set_external_change_state(EditorDocumentId a_documentId,
                                                         ExternalChangeState a_state) noexcept;
    /// @brief Document Close を開始し、明示判断の要否を状態として返す
    [[nodiscard]] Result<DocumentCloseState> request_close(EditorDocumentId a_documentId) noexcept;
    /// @brief AwaitingDecision 中の Save、Discard、Cancel 判断を適用する
    [[nodiscard]] Result<DocumentCloseState> respond_to_close(EditorDocumentId a_documentId,
                                                              CloseDecision a_decision) noexcept;

  private:
    /// @brief Document Identity に対応する可変 Document を返す
    [[nodiscard]] EditorDocument *find_document(EditorDocumentId a_id) noexcept;
    /// @brief Closed Document の所有 Data を Session から解放する
    void erase_closed_document(EditorDocumentId a_id) noexcept;
    /// @brief 現在 Thread が Controller 作成 Thread であることを全構成で検証する
    void assert_owner_thread() const noexcept;
    /// @brief Allocation 失敗を Fatal 境界へ変換する
    [[noreturn]] void terminate_allocation() const noexcept;
    /// @brief 予期しない例外を Fatal 境界へ変換する
    [[noreturn]] void terminate_exception() const noexcept;

    std::shared_ptr<const DocumentStateOrigin> m_stateOrigin;
    ProjectWorkspaceSession m_session;
    const AssertContext *m_assertContext;
    std::thread::id m_ownerThread;
    std::uint64_t m_nextDocumentId = 1U;
};
} // namespace cue::editor_core
