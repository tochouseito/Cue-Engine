#pragma once

#include <Cue/IO/RelativePath.h>
#include <Cue/Scene/SceneDocument.h>

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cue::editor_core
{
class EditorController;
class EditorDocument;
class DocumentStateOrigin;

/// @brief Editor Process 内で開いている Document を識別する一時 Identity
class EditorDocumentId final
{
  public:
    /// @brief Session が発行した非永続値を保持する
    explicit constexpr EditorDocumentId(std::uint64_t a_value) noexcept : m_value(a_value)
    {
    }

    /// @brief Session 内の数値表現を返す
    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return m_value;
    }

    /// @brief Document Identity 値を比較する
    [[nodiscard]] constexpr auto operator<=>(const EditorDocumentId &) const noexcept = default;

  private:
    std::uint64_t m_value;
};

/// @brief 一つの EditorDocument 内で再利用しない Authoring 状態 Identity
class DocumentStateId final
{
  public:
    DocumentStateId(const DocumentStateId &) noexcept = default;
    DocumentStateId &operator=(const DocumentStateId &) noexcept = default;
    DocumentStateId(DocumentStateId &&) noexcept = default;
    DocumentStateId &operator=(DocumentStateId &&) noexcept = default;
    ~DocumentStateId() noexcept = default;

    /// @brief State を発行した Editor Document Identity を返す
    [[nodiscard]] constexpr EditorDocumentId document_id() const noexcept
    {
        return m_documentId;
    }

    /// @brief Document 内の数値表現を返す
    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return m_value;
    }

    /// @brief Authoring 状態 Identity 値を比較する
    [[nodiscard]] auto operator<=>(const DocumentStateId &) const noexcept = default;

  private:
    friend class EditorController;
    friend class EditorDocument;

    /// @brief Controller 固有 Origin、発行元 Document、非永続 Revision 値を保持する
    DocumentStateId(std::shared_ptr<const DocumentStateOrigin> a_origin, EditorDocumentId a_documentId,
                    std::uint64_t a_value) noexcept
        : m_origin(std::move(a_origin)), m_documentId(a_documentId), m_value(a_value)
    {
    }

    std::shared_ptr<const DocumentStateOrigin> m_origin;
    EditorDocumentId m_documentId;
    std::uint64_t m_value;
};

/// @brief 開いた Scene 正本に対する外部変更の観測状態
enum class ExternalChangeState : std::uint8_t
{
    None,
    Modified,
    Removed
};

/// @brief Close 要求に対する Editor Core の進行状態
enum class DocumentCloseState : std::uint8_t
{
    Open,
    AwaitingDecision,
    SaveRequested,
    Closed
};

/// @brief Dirty または競合中 Document を閉じるための利用者判断
enum class CloseDecision : std::uint8_t
{
    Save,
    Discard,
    Cancel
};

/// @brief 一つの SceneDocument と UI 非依存 Editor Session 状態の一意 Owner
///
/// 公開 View は Owner Thread 上で使用し、次の Controller Mutation または Controller 破棄より長く保持しない
class EditorDocument final
{
  public:
    EditorDocument(const EditorDocument &) = delete;
    EditorDocument &operator=(const EditorDocument &) = delete;
    EditorDocument(EditorDocument &&) noexcept = default;
    EditorDocument &operator=(EditorDocument &&) noexcept = default;
    ~EditorDocument() noexcept = default;

    /// @brief Editor Session 内の一時 Document Identity を返す
    [[nodiscard]] EditorDocumentId id() const noexcept;
    /// @brief 永続 Authoring Scene の Read-only 正本を返す
    [[nodiscard]] const scene::SceneDocument &scene_document() const noexcept;
    /// @brief Project Source Assets Root から解決する Scene Locator を返す
    [[nodiscard]] const RelativePath &scene_locator() const noexcept;
    /// @brief Scene 正本の保存先が確定済みか返す
    [[nodiscard]] bool has_saved_destination() const noexcept;
    /// @brief 現在表示している Authoring 状態 Identity を返す
    [[nodiscard]] DocumentStateId current_state_id() const noexcept;
    /// @brief 最後に確定保存された Authoring 状態 Identity を返す
    [[nodiscard]] DocumentStateId saved_state_id() const noexcept;
    /// @brief 現在状態と保存状態の Revision 差だけから Dirty を判定する
    [[nodiscard]] bool is_dirty() const noexcept;
    /// @brief 次の Controller Mutation まで有効な選択 Object 列を返す
    [[nodiscard]] std::span<const scene::ObjectId> selection() const noexcept;
    /// @brief Primary Selection があれば次の Controller Mutation まで有効な Pointer を返す
    [[nodiscard]] const scene::ObjectId *try_primary_selection() const noexcept;
    /// @brief Scene 正本に対する外部変更の観測状態を返す
    [[nodiscard]] ExternalChangeState external_change_state() const noexcept;
    /// @brief Close Workflow の現在状態を返す
    [[nodiscard]] DocumentCloseState close_state() const noexcept;
    /// @brief Close 前に Save、Discard、Cancel の明示判断が必要か返す
    [[nodiscard]] bool requires_close_decision() const noexcept;

  private:
    friend class EditorController;

    /// @brief Open 済み Scene と初期 Session 状態を束ねる
    EditorDocument(EditorDocumentId a_id, std::shared_ptr<const DocumentStateOrigin> a_stateOrigin,
                   scene::SceneDocument &&a_document, RelativePath &&a_locator, bool a_hasSavedDestination) noexcept;

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
