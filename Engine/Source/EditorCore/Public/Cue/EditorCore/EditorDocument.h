#pragma once

#include <Cue/IO/RelativePath.h>
#include <Cue/Scene/SceneDocument.h>

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cue::editor_core
{
/// @brief Editor Process内で開いているDocumentを識別する一時Identity
class EditorDocumentId final
{
  public:
    /// @brief Sessionが発行した非永続値を保持する
    explicit constexpr EditorDocumentId(std::uint64_t a_value) noexcept : m_value(a_value)
    {
    }

    /// @brief Session内の数値表現を返す
    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return m_value;
    }

    /// @brief Document Identity値を比較する
    [[nodiscard]] constexpr auto operator<=>(const EditorDocumentId &) const noexcept = default;

  private:
    std::uint64_t m_value;
};

/// @brief 一つのEditorDocument内で再利用しないAuthoring状態Identity
class DocumentStateId final
{
  public:
    /// @brief 発行元Documentと非永続Revision値を保持する
    constexpr DocumentStateId(EditorDocumentId a_documentId, std::uint64_t a_value) noexcept
        : m_documentId(a_documentId), m_value(a_value)
    {
    }

    /// @brief Stateを発行したEditor Document Identityを返す
    [[nodiscard]] constexpr EditorDocumentId document_id() const noexcept
    {
        return m_documentId;
    }

    /// @brief Document内の数値表現を返す
    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return m_value;
    }

    /// @brief Authoring状態Identity値を比較する
    [[nodiscard]] constexpr auto operator<=>(const DocumentStateId &) const noexcept = default;

  private:
    EditorDocumentId m_documentId;
    std::uint64_t m_value;
};

/// @brief 開いたScene正本に対する外部変更の観測状態
enum class ExternalChangeState : std::uint8_t
{
    None,
    Modified,
    Removed
};

/// @brief Close要求に対するEditor Coreの進行状態
enum class DocumentCloseState : std::uint8_t
{
    Open,
    AwaitingDecision,
    SaveRequested,
    Closed
};

/// @brief Dirtyまたは競合中Documentを閉じるための利用者判断
enum class CloseDecision : std::uint8_t
{
    Save,
    Discard,
    Cancel
};

class EditorController;

/// @brief 一つのSceneDocumentとUI非依存Editor Session状態の一意Owner
///
/// 公開ViewはOwner Thread上で使用し、次のController MutationまたはController破棄より長く保持しない
class EditorDocument final
{
  public:
    EditorDocument(const EditorDocument &) = delete;
    EditorDocument &operator=(const EditorDocument &) = delete;
    EditorDocument(EditorDocument &&) noexcept = default;
    EditorDocument &operator=(EditorDocument &&) noexcept = default;
    ~EditorDocument() noexcept = default;

    /// @brief Editor Session内の一時Document Identityを返す
    [[nodiscard]] EditorDocumentId id() const noexcept;
    /// @brief 永続Authoring SceneのRead-only正本を返す
    [[nodiscard]] const scene::SceneDocument &scene_document() const noexcept;
    /// @brief Project Source Assets Rootから解決するScene Locatorを返す
    [[nodiscard]] const RelativePath &scene_locator() const noexcept;
    /// @brief Scene正本の保存先が確定済みか返す
    [[nodiscard]] bool has_saved_destination() const noexcept;
    /// @brief 現在表示しているAuthoring状態Identityを返す
    [[nodiscard]] DocumentStateId current_state_id() const noexcept;
    /// @brief 最後に確定保存されたAuthoring状態Identityを返す
    [[nodiscard]] DocumentStateId saved_state_id() const noexcept;
    /// @brief 現在状態と保存状態のRevision差だけからDirtyを判定する
    [[nodiscard]] bool is_dirty() const noexcept;
    /// @brief 次のController Mutationまで有効な選択Object列を返す
    [[nodiscard]] std::span<const scene::ObjectId> selection() const noexcept;
    /// @brief Primary Selectionがあれば次のController Mutationまで有効なPointerを返す
    [[nodiscard]] const scene::ObjectId *try_primary_selection() const noexcept;
    /// @brief Scene正本に対する外部変更の観測状態を返す
    [[nodiscard]] ExternalChangeState external_change_state() const noexcept;
    /// @brief Close Workflowの現在状態を返す
    [[nodiscard]] DocumentCloseState close_state() const noexcept;
    /// @brief Close前にSave、Discard、Cancelの明示判断が必要か返す
    [[nodiscard]] bool requires_close_decision() const noexcept;

  private:
    friend class EditorController;

    /// @brief Open済みSceneと初期Session状態を束ねる
    EditorDocument(EditorDocumentId a_id, scene::SceneDocument &&a_document, RelativePath &&a_locator,
                   bool a_hasSavedDestination) noexcept;

    EditorDocumentId m_id;
    scene::SceneDocument m_document;
    RelativePath m_locator;
    bool m_hasSavedDestination;
    DocumentStateId m_currentStateId;
    DocumentStateId m_savedStateId;
    std::uint64_t m_nextStateId = 2U;
    std::vector<scene::ObjectId> m_selection;
    std::optional<scene::ObjectId> m_primarySelection;
    ExternalChangeState m_externalChangeState = ExternalChangeState::None;
    DocumentCloseState m_closeState = DocumentCloseState::Open;
};
} // namespace cue::editor_core
