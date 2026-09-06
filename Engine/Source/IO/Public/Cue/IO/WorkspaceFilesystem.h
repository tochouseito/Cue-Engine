#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/IO/RelativePath.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
class AssertContext;
class WorkspaceDirectory;
class WorkspaceFilesystem;

/// @brief 検証済みUser Locatorと列挙結果からだけ生成できるRoot相対内部Path
class BoundWorkspacePath final
{
  public:
    /// @brief Root相対のPortable Path文字列を返す
    [[nodiscard]] std::string_view text() const noexcept;

  private:
    friend class WorkspaceDirectory;
    friend class WorkspaceFilesystem;

    /// @brief 検証済みの合成済みPath文字列と発行元Workspaceを所有する
    BoundWorkspacePath(std::string a_text, std::uint64_t a_bindingToken) noexcept;

    std::string m_text;
    std::uint64_t m_bindingToken;
};

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

/// @brief Namespace Mutation後に確定したPortableな観測結果
enum class WorkspaceMutationOutcome : std::uint8_t
{
    Committed,
    NotCommitted,
    CommittedButDurabilityUnknown,
    ReconciliationRequired
};

/// @brief Workspace Adapterが返すMutation結果とCleanup診断
struct WorkspaceMutationResult final
{
    WorkspaceMutationOutcome outcome = WorkspaceMutationOutcome::ReconciliationRequired;
    std::optional<Error> primaryError;
    std::vector<Error> secondaryDiagnostics;
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

    /// @brief 列挙で生成済みの合成PathをDirectory Locatorとして所有する
    [[nodiscard]] static WorkspaceDirectory from_bound_path(BoundWorkspacePath a_locator) noexcept;

    /// @brief Workspace Root自体を表すか判定する
    [[nodiscard]] bool is_root() const noexcept;

    /// @brief Root相対Locatorを返し、Root自体ならnullptrを返す
    [[nodiscard]] const BoundWorkspacePath *locator() const noexcept;

  private:
    /// @brief Optional Locatorから列挙対象を構築する
    explicit WorkspaceDirectory(std::optional<BoundWorkspacePath> a_locator) noexcept;

    std::optional<BoundWorkspacePath> m_locator;
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
    std::optional<BoundWorkspacePath> locator;
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

    /// @brief 検証済みUser LocatorをこのWorkspace固有のDirectory CapabilityへBindingする
    [[nodiscard]] Result<WorkspaceDirectory> bind_directory(RelativePath a_locator,
                                                            const AssertContext &a_assertContext) const noexcept;

    /// @brief 個別検証済みArea Rootと利用者Locatorを内部Pathへ合成する
    [[nodiscard]] Result<BoundWorkspacePath> bind_path(RelativePath a_areaRoot, RelativePath a_locator,
                                                       const AssertContext &a_assertContext) const noexcept;

    /// @brief 検証済みRoot相対LocatorをこのWorkspace固有の内部PathへBindingする
    [[nodiscard]] Result<BoundWorkspacePath> bind_root_path(RelativePath a_locator,
                                                            const AssertContext &a_assertContext) const noexcept;

    /// @brief Directory直下を決定的に列挙する
    [[nodiscard]] virtual Result<DirectorySnapshot> list_directory(const WorkspaceDirectory &a_directory,
                                                                   TraversalLimits a_limits) noexcept = 0;

    /// @brief Directory Capabilityが現在も同じRoot内の通常Directoryを指すか検証する
    [[nodiscard]] virtual Result<void> verify_directory(const WorkspaceDirectory &a_directory) noexcept = 0;

    /// @brief Root内Regular Fileを排他的な読取りSnapshotとして上限付きで取得する
    [[nodiscard]] virtual Result<std::vector<std::byte>> read_file_bounded(const BoundWorkspacePath &a_source,
                                                                           std::size_t a_maxBytes) noexcept = 0;

    /// @brief Operation所有Sibling Directoryを経由し、既存Entryを上書きせずDirectoryを公開する
    [[nodiscard]] virtual WorkspaceMutationResult create_directory_new(const BoundWorkspacePath &a_destination,
                                                                       std::string_view a_operationId) noexcept = 0;

    /// @brief Operation所有Temporary Fileを経由し、既存Entryを上書きせずFileをAtomic公開する
    [[nodiscard]] virtual WorkspaceMutationResult create_file_new_atomic(const BoundWorkspacePath &a_destination,
                                                                         std::span<const std::byte> a_bytes,
                                                                         std::string_view a_operationId) noexcept = 0;

  protected:
    /// @brief 発行可能なRoot相対Path長を固定してCapabilityを初期化する
    explicit WorkspaceFilesystem(std::size_t a_maxBoundPathCharacters) noexcept;

    /// @brief DirectoryがRootまたはこのWorkspaceから発行済みか判定する
    [[nodiscard]] bool owns_directory(const WorkspaceDirectory &a_directory) const noexcept;

    /// @brief 内部PathがこのWorkspaceから発行済みか判定する
    [[nodiscard]] bool owns_path(const BoundWorkspacePath &a_path) const noexcept;

    /// @brief このWorkspaceが発行した内部Pathから親Directory Capabilityを構築する
    [[nodiscard]] Result<WorkspaceDirectory> parent_directory(const BoundWorkspacePath &a_path,
                                                              const AssertContext &a_assertContext) const noexcept;

    /// @brief 親内部Pathへ個別検証済みChild Locatorを合成する
    [[nodiscard]] Result<BoundWorkspacePath> append_path(const WorkspaceDirectory &a_parent, RelativePath a_child,
                                                         const AssertContext &a_assertContext) const noexcept;

  private:
    std::uint64_t m_bindingToken;
    std::size_t m_maxBoundPathCharacters;
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
