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
/// @brief 一つのProject Descriptorと開いたEditorDocument群の一意Owner
class ProjectWorkspaceSession final
{
  public:
    ProjectWorkspaceSession(const ProjectWorkspaceSession &) = delete;
    ProjectWorkspaceSession &operator=(const ProjectWorkspaceSession &) = delete;
    ProjectWorkspaceSession(ProjectWorkspaceSession &&) noexcept = default;
    ProjectWorkspaceSession &operator=(ProjectWorkspaceSession &&) noexcept = default;
    ~ProjectWorkspaceSession() noexcept = default;

    /// @brief Sessionが所有するProject Descriptorを返す
    [[nodiscard]] const ProjectDescriptor &project_descriptor() const noexcept;
    /// @brief 次のDocument OpenまたはController破棄まで有効なDocument Viewを返す
    [[nodiscard]] std::span<const EditorDocument> documents() const noexcept;
    /// @brief Document Identityに対応するRead-only Documentを返す
    [[nodiscard]] const EditorDocument *find_document(EditorDocumentId a_id) const noexcept;

  private:
    friend class EditorController;

    /// @brief 検証済みProject Descriptorの所有権をSessionへ移す
    explicit ProjectWorkspaceSession(ProjectDescriptor &&a_descriptor) noexcept;

    ProjectDescriptor m_descriptor;
    std::vector<EditorDocument> m_documents;
};

/// @brief Editor IntentをUI非依存Document状態遷移へ変換するOwner Thread Controller
///
/// Controller、返したSession View、Document Viewは作成Threadだけで使用する
class EditorController final
{
  public:
    EditorController(const EditorController &) = delete;
    EditorController &operator=(const EditorController &) = delete;
    EditorController(EditorController &&) = delete;
    EditorController &operator=(EditorController &&) = delete;
    ~EditorController() noexcept;

    /// @brief Project Descriptorの所有権を受け取って空Workspace Sessionを生成する
    /// @param a_assertContext Controllerより長く生存させる診断Context
    [[nodiscard]] static std::unique_ptr<EditorController> create(ProjectDescriptor &&a_descriptor,
                                                                  const AssertContext &a_assertContext) noexcept;

    /// @brief Project Workspace SessionのRead-only Viewを返す
    [[nodiscard]] const ProjectWorkspaceSession &session() const noexcept;

    /// @brief 検証済みSceneDocumentを一意なSceneとLocatorで開く
    [[nodiscard]] Result<EditorDocumentId> open_document(scene::SceneDocument &&a_document, RelativePath &&a_locator,
                                                         bool a_hasSavedDestination) noexcept;
    /// @brief Stable ObjectIdだけを保持し、存在しないIDと重複を安全に除く
    [[nodiscard]] Result<void> set_selection(EditorDocumentId a_documentId,
                                             std::span<const scene::ObjectId> a_objectIds,
                                             const scene::ObjectId *a_primaryObjectId = nullptr) noexcept;
    /// @brief SelectionとPrimary Selectionを空にする
    [[nodiscard]] Result<void> clear_selection(EditorDocumentId a_documentId) noexcept;
    /// @brief 現在Sceneに存在しない選択IDを除去する
    [[nodiscard]] Result<void> reconcile_selection(EditorDocumentId a_documentId) noexcept;
    /// @brief 成功した永続Data変更に一つの新しいState Identityを発行する
    [[nodiscard]] Result<DocumentStateId> record_persistent_change(EditorDocumentId a_documentId) noexcept;
    /// @brief 確定保存された発行済みStateを保存地点として記録する
    [[nodiscard]] Result<void> mark_saved(EditorDocumentId a_documentId, DocumentStateId a_savedStateId) noexcept;
    /// @brief Scene正本に対する外部変更の観測状態を更新する
    [[nodiscard]] Result<void> set_external_change_state(EditorDocumentId a_documentId,
                                                         ExternalChangeState a_state) noexcept;
    /// @brief Document Closeを開始し、明示判断の要否を状態として返す
    [[nodiscard]] Result<DocumentCloseState> request_close(EditorDocumentId a_documentId) noexcept;
    /// @brief AwaitingDecision中のSave、Discard、Cancel判断を適用する
    [[nodiscard]] Result<DocumentCloseState> respond_to_close(EditorDocumentId a_documentId,
                                                              CloseDecision a_decision) noexcept;

  private:
    /// @brief Project DescriptorとOwner Thread契約を保持する
    EditorController(ProjectDescriptor &&a_descriptor, const AssertContext &a_assertContext) noexcept;
    /// @brief Document Identityに対応する可変Documentを返す
    [[nodiscard]] EditorDocument *find_document(EditorDocumentId a_id) noexcept;
    /// @brief Closed Documentの所有DataをSessionから解放する
    void erase_closed_document(EditorDocumentId a_id) noexcept;
    /// @brief 現在ThreadがController作成Threadであることを全構成で検証する
    void assert_owner_thread() const noexcept;
    /// @brief Allocation失敗をFatal境界へ変換する
    [[noreturn]] void terminate_allocation() const noexcept;
    /// @brief 予期しない例外をFatal境界へ変換する
    [[noreturn]] void terminate_exception() const noexcept;

    ProjectWorkspaceSession m_session;
    const AssertContext *m_assertContext;
    std::thread::id m_ownerThread;
    std::uint64_t m_nextDocumentId = 1U;
};
} // namespace cue::editor_core
