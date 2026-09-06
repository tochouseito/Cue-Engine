#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/IO/RelativePath.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
class AssertContext;

/// @brief Workspace列挙で公開するPortable Entry種別
enum class WorkspaceEntryType : std::uint8_t
{
    Directory,
    RegularFile,
    UnsupportedEntry
};

/// @brief 不完全なSnapshotで再列挙理由をPortableに分類する
enum class WorkspaceDiagnosticCode : std::uint8_t
{
    UnsupportedName,
    ReparsePoint,
    EntryDisappeared,
    PermissionDenied,
    TypeChanged,
    EnumerationFailed
};

/// @brief Directory Snapshotが完全か再列挙を要するかを表す
enum class WorkspaceSnapshotState : std::uint8_t
{
    Complete,
    RescanRequired
};

/// @brief 再帰処理のCaller上限を全て非Zero値で保持する
struct TraversalLimits final
{
    std::size_t maxDepth = 0U;
    std::size_t maxVisitedEntries = 0U;
    std::size_t maxResults = 0U;
    std::size_t maxMetadataBytes = 0U;

    /// @brief 全ての上限が非Zeroか判定する
    [[nodiscard]] bool is_valid() const noexcept;
};

/// @brief Root自体または検証済みRoot相対Directoryを表す
class WorkspaceDirectory final
{
  public:
    /// @brief Workspace Root自体を列挙対象として返す
    [[nodiscard]] static WorkspaceDirectory root() noexcept;

    /// @brief 検証済みRoot相対Locatorを列挙対象として所有する
    [[nodiscard]] static WorkspaceDirectory from_locator(RelativePath a_locator) noexcept;

    /// @brief Workspace Root自体を表すか判定する
    [[nodiscard]] bool is_root() const noexcept;

    /// @brief Root相対Locatorを返し、Root自体ならnullptrを返す
    [[nodiscard]] const RelativePath *locator() const noexcept;

  private:
    /// @brief Optional Locatorから列挙対象を構築する
    explicit WorkspaceDirectory(std::optional<RelativePath> a_locator) noexcept;

    std::optional<RelativePath> m_locator;
};

/// @brief Entry単位で発生した列挙競合または拒否理由を保持する
struct WorkspaceDiagnostic final
{
    std::uint64_t generation = 0U;
    WorkspaceDiagnosticCode code = WorkspaceDiagnosticCode::EnumerationFailed;
    std::string displayName;
    std::int64_t nativeCode = 0;
};

/// @brief 正規化済みDirectory Entry Metadataを所有する
struct WorkspaceEntry final
{
    std::uint64_t parentGeneration = 0U;
    std::string displayName;
    std::string sortKey;
    std::optional<RelativePath> locator;
    WorkspaceEntryType type = WorkspaceEntryType::UnsupportedEntry;
    std::uint64_t byteSize = 0U;
    std::optional<WorkspaceDiagnosticCode> rejection;

    /// @brief Mutationへ渡せる検証済みLocatorを保持するか判定する
    [[nodiscard]] bool is_operable() const noexcept;
};

/// @brief Directory直下を一回のGenerationで取得した所有Snapshot
struct DirectorySnapshot final
{
    std::uint64_t generation = 0U;
    WorkspaceSnapshotState state = WorkspaceSnapshotState::Complete;
    std::vector<WorkspaceEntry> entries;
    std::vector<WorkspaceDiagnostic> diagnostics;
};

/// @brief Bounded Searchの決定的な所有結果
struct WorkspaceSearchResult final
{
    WorkspaceSnapshotState state = WorkspaceSnapshotState::Complete;
    std::vector<WorkspaceEntry> entries;
    std::vector<WorkspaceDiagnostic> diagnostics;
    std::size_t visitedEntries = 0U;
};

/// @brief Root境界内の列挙CapabilityをPlatform実装へ分離する契約
///
/// InstanceはThread-safeではなく、同一Instanceの並行利用は呼び出し側が同期する
class WorkspaceFilesystem
{
  public:
    /// @brief Root Capabilityの一意所有を保つためCopy構築を禁止する
    WorkspaceFilesystem(const WorkspaceFilesystem &) = delete;
    /// @brief Root Capabilityの一意所有を保つためCopy代入を禁止する
    WorkspaceFilesystem &operator=(const WorkspaceFilesystem &) = delete;
    /// @brief Polymorphic RootのNative Resourceを実装側で解放する
    virtual ~WorkspaceFilesystem() = default;

    /// @brief Adapter固有Hard Limitを返す
    [[nodiscard]] virtual TraversalLimits hard_limits() const noexcept = 0;

    /// @brief Directory直下を決定的に列挙する
    [[nodiscard]] virtual Result<DirectorySnapshot> list_directory(const WorkspaceDirectory &a_directory,
                                                                   TraversalLimits a_limits) noexcept = 0;

  protected:
    /// @brief Abstract Capabilityの初期化だけをDerived実装へ許可する
    WorkspaceFilesystem() noexcept = default;
    /// @brief Abstract Capabilityを直接移動させずDerived所有権で管理する
    WorkspaceFilesystem(WorkspaceFilesystem &&) noexcept = default;
    /// @brief Abstract Capabilityを直接移動代入させずDerived所有権で管理する
    WorkspaceFilesystem &operator=(WorkspaceFilesystem &&) noexcept = default;
};

/// @brief Snapshot EntryをDirectory、File、UnsupportedのPortable順へ整列する
void sort_workspace_entries(std::vector<WorkspaceEntry> &a_entries) noexcept;

/// @brief 正規化済みSnapshotへASCII大小文字非依存Filterと結果上限を適用する
[[nodiscard]] Result<std::vector<WorkspaceEntry>> filter_workspace_snapshot(
    const DirectorySnapshot &a_snapshot, std::string_view a_filter, std::size_t a_maxResults,
    const AssertContext &a_assertContext) noexcept;

/// @brief Root境界内を決定的かつ上限制御付きで再帰検索する
[[nodiscard]] Result<WorkspaceSearchResult> search_workspace(WorkspaceFilesystem &a_filesystem,
                                                             const WorkspaceDirectory &a_startDirectory,
                                                             std::string_view a_filter, TraversalLimits a_limits,
                                                             const AssertContext &a_assertContext) noexcept;
} // namespace cue
