#include <Cue/IO/WorkspaceFilesystem.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/Error.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

namespace
{
/// @brief noexcept処理中のAllocation失敗をProject Fatal Policyへ接続する
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Workspace filesystem allocation failed");
    std::abort();
}

/// @brief Entry種別をPortable Sort順へ変換する
[[nodiscard]] std::uint8_t entry_rank(cue::WorkspaceEntryType a_type) noexcept
{
    switch (a_type)
    {
    case cue::WorkspaceEntryType::Directory:
        return 0U;
    case cue::WorkspaceEntryType::RegularFile:
        return 1U;
    case cue::WorkspaceEntryType::UnsupportedEntry:
        return 2U;
    }
    return 2U;
}

/// @brief ASCIIだけをLocale非依存Lowercaseへ変換する
[[nodiscard]] char ascii_lower(char a_character) noexcept
{
    if (a_character >= 'A' && a_character <= 'Z')
    {
        return static_cast<char>(a_character + ('a' - 'A'));
    }
    return a_character;
}

/// @brief UTF-8 Byte列中のASCIIを大小文字非依存として部分一致判定する
[[nodiscard]] bool matches_filter(std::string_view a_text, std::string_view a_filter) noexcept
{
    if (a_filter.empty())
    {
        return true;
    }
    if (a_filter.size() > a_text.size())
    {
        return false;
    }
    for (std::size_t start = 0U; start <= a_text.size() - a_filter.size(); ++start)
    {
        bool matches = true;
        for (std::size_t index = 0U; index < a_filter.size(); ++index)
        {
            if (ascii_lower(a_text[start + index]) != ascii_lower(a_filter[index]))
            {
                matches = false;
                break;
            }
        }
        if (matches)
        {
            return true;
        }
    }
    return false;
}

/// @brief Snapshot Entryが使用するBounded Metadata Byte数を返す
[[nodiscard]] std::size_t metadata_bytes(const cue::WorkspaceEntry &a_entry) noexcept
{
    const std::size_t locatorBytes = a_entry.locator.has_value() ? a_entry.locator->text().size() : 0U;
    constexpr std::size_t k_fixedBytes = sizeof(cue::WorkspaceEntry);
    if (a_entry.displayName.size() > std::numeric_limits<std::size_t>::max() - k_fixedBytes ||
        k_fixedBytes + a_entry.displayName.size() > std::numeric_limits<std::size_t>::max() - a_entry.sortKey.size() ||
        k_fixedBytes + a_entry.displayName.size() + a_entry.sortKey.size() >
            std::numeric_limits<std::size_t>::max() - locatorBytes)
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return k_fixedBytes + a_entry.displayName.size() + a_entry.sortKey.size() + locatorBytes;
}

/// @brief Search診断が使用するBounded Metadata Byte数を返す
[[nodiscard]] std::size_t metadata_bytes(const cue::WorkspaceDiagnostic &a_diagnostic) noexcept
{
    if (a_diagnostic.displayName.size() > std::numeric_limits<std::size_t>::max() - sizeof(cue::WorkspaceDiagnostic))
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return sizeof(cue::WorkspaceDiagnostic) + a_diagnostic.displayName.size();
}

/// @brief 二つのTraversal Limitの小さい方を要素ごとに返す
[[nodiscard]] cue::TraversalLimits intersect_limits(cue::TraversalLimits a_caller, cue::TraversalLimits a_hard) noexcept
{
    return cue::TraversalLimits{
        std::min(a_caller.maxDepth, a_hard.maxDepth), std::min(a_caller.maxVisitedEntries, a_hard.maxVisitedEntries),
        std::min(a_caller.maxResults, a_hard.maxResults), std::min(a_caller.maxMetadataBytes, a_hard.maxMetadataBytes)};
}

/// @brief Search Stack上のDirectoryとRootからのDepthを所有する
struct PendingDirectory final
{
    cue::WorkspaceDirectory directory;
    std::size_t depth = 0U;
};
} // namespace

namespace cue
{
bool TraversalLimits::is_valid() const noexcept
{
    return maxDepth != 0U && maxVisitedEntries != 0U && maxResults != 0U && maxMetadataBytes != 0U;
}

BoundWorkspacePath BoundWorkspacePath::from_locator(RelativePath a_locator,
                                                    const AssertContext &a_assertContext) noexcept
{
    try
    {
        return BoundWorkspacePath(std::string(a_locator.text()));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

std::string_view BoundWorkspacePath::text() const noexcept
{
    return m_text;
}

BoundWorkspacePath::BoundWorkspacePath(std::string a_text) noexcept : m_text(std::move(a_text))
{
}

WorkspaceDirectory WorkspaceDirectory::root() noexcept
{
    return WorkspaceDirectory(std::nullopt);
}

WorkspaceDirectory WorkspaceDirectory::from_locator(RelativePath a_locator,
                                                    const AssertContext &a_assertContext) noexcept
{
    return WorkspaceDirectory(BoundWorkspacePath::from_locator(std::move(a_locator), a_assertContext));
}

WorkspaceDirectory WorkspaceDirectory::from_bound_path(BoundWorkspacePath a_locator) noexcept
{
    return WorkspaceDirectory(std::move(a_locator));
}

bool WorkspaceDirectory::is_root() const noexcept
{
    return !m_locator.has_value();
}

const BoundWorkspacePath *WorkspaceDirectory::locator() const noexcept
{
    return m_locator.has_value() ? &*m_locator : nullptr;
}

WorkspaceDirectory::WorkspaceDirectory(std::optional<BoundWorkspacePath> a_locator) noexcept
    : m_locator(std::move(a_locator))
{
}

BoundWorkspacePath append_workspace_path(const WorkspaceDirectory &a_parent, RelativePath a_child,
                                         const AssertContext &a_assertContext) noexcept
{
    try
    {
        std::string text;
        const BoundWorkspacePath *parent = a_parent.locator();
        if (parent != nullptr)
        {
            text.assign(parent->text());
            text.push_back('/');
        }
        text.append(a_child.text());
        return BoundWorkspacePath(std::move(text));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

bool WorkspaceEntry::is_operable() const noexcept
{
    return locator.has_value() && !rejection.has_value() && type != WorkspaceEntryType::UnsupportedEntry;
}

void sort_workspace_entries(std::vector<WorkspaceEntry> &a_entries) noexcept
{
    std::sort(a_entries.begin(), a_entries.end(),
              [](const WorkspaceEntry &a_left, const WorkspaceEntry &a_right)
              {
                  const std::uint8_t leftRank = entry_rank(a_left.type);
                  const std::uint8_t rightRank = entry_rank(a_right.type);
                  if (leftRank != rightRank)
                  {
                      return leftRank < rightRank;
                  }
                  if (a_left.sortKey != a_right.sortKey)
                  {
                      return a_left.sortKey < a_right.sortKey;
                  }
                  return a_left.displayName < a_right.displayName;
              });
}

Result<std::vector<WorkspaceEntry>> filter_workspace_snapshot(const DirectorySnapshot &a_snapshot,
                                                              std::string_view a_filter, std::size_t a_maxResults,
                                                              const AssertContext &a_assertContext) noexcept
{
    if (a_maxResults == 0U)
    {
        return Result<std::vector<WorkspaceEntry>>::failure(make_io_error(
            a_assertContext, IoError::CapacityExceeded, "Workspace filter result limit must be non-zero"));
    }

    std::vector<WorkspaceEntry> filtered;
    try
    {
        filtered.reserve(std::min(a_snapshot.entries.size(), a_maxResults));
        for (const WorkspaceEntry &entry : a_snapshot.entries)
        {
            if (!matches_filter(entry.displayName, a_filter))
            {
                continue;
            }
            if (filtered.size() == a_maxResults)
            {
                return Result<std::vector<WorkspaceEntry>>::failure(make_io_error(
                    a_assertContext, IoError::CapacityExceeded, "Workspace filter result limit was exceeded"));
            }
            filtered.push_back(entry);
        }
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    return Result<std::vector<WorkspaceEntry>>::success(std::move(filtered));
}

Result<WorkspaceSearchResult> search_workspace(WorkspaceFilesystem &a_filesystem,
                                               const WorkspaceDirectory &a_startDirectory, std::string_view a_filter,
                                               TraversalLimits a_limits, const AssertContext &a_assertContext) noexcept
{
    const TraversalLimits hardLimits = a_filesystem.hard_limits();
    if (!a_limits.is_valid() || !hardLimits.is_valid())
    {
        return Result<WorkspaceSearchResult>::failure(
            make_io_error(a_assertContext, IoError::CapacityExceeded, "Workspace search limits must all be non-zero"));
    }
    const TraversalLimits effective = intersect_limits(a_limits, hardLimits);

    WorkspaceSearchResult result;
    std::vector<PendingDirectory> pending;
    std::size_t metadataBytes = 0U;
    try
    {
        pending.push_back(PendingDirectory{a_startDirectory, 1U});
        while (!pending.empty())
        {
            PendingDirectory current = std::move(pending.back());
            pending.pop_back();

            TraversalLimits directLimits = effective;
            directLimits.maxDepth = 1U;
            directLimits.maxVisitedEntries = effective.maxVisitedEntries - result.visitedEntries;
            directLimits.maxResults = directLimits.maxVisitedEntries;
            directLimits.maxMetadataBytes = effective.maxMetadataBytes - metadataBytes;
            if (directLimits.maxVisitedEntries == 0U || directLimits.maxResults == 0U ||
                directLimits.maxMetadataBytes == 0U)
            {
                return Result<WorkspaceSearchResult>::failure(make_io_error(
                    a_assertContext, IoError::CapacityExceeded, "Workspace search traversal limit was exceeded"));
            }

            Result<DirectorySnapshot> listed = a_filesystem.list_directory(current.directory, directLimits);
            if (!listed)
            {
                return Result<WorkspaceSearchResult>::failure(std::move(*listed.try_error()));
            }
            DirectorySnapshot snapshot = std::move(*listed.try_value());
            if (snapshot.state == WorkspaceSnapshotState::RescanRequired)
            {
                result.state = WorkspaceSnapshotState::RescanRequired;
            }
            for (const WorkspaceDiagnostic &diagnostic : snapshot.diagnostics)
            {
                const std::size_t diagnosticBytes = metadata_bytes(diagnostic);
                if (diagnosticBytes > effective.maxMetadataBytes - metadataBytes)
                {
                    return Result<WorkspaceSearchResult>::failure(make_io_error(
                        a_assertContext, IoError::CapacityExceeded, "Workspace search metadata limit was exceeded"));
                }
                metadataBytes += diagnosticBytes;
            }
            result.diagnostics.insert(result.diagnostics.end(), std::make_move_iterator(snapshot.diagnostics.begin()),
                                      std::make_move_iterator(snapshot.diagnostics.end()));

            std::vector<WorkspaceDirectory> children;
            for (WorkspaceEntry &entry : snapshot.entries)
            {
                if (result.visitedEntries == effective.maxVisitedEntries)
                {
                    return Result<WorkspaceSearchResult>::failure(
                        make_io_error(a_assertContext, IoError::CapacityExceeded,
                                      "Workspace search visited-entry limit was exceeded"));
                }
                ++result.visitedEntries;
                const std::size_t entryBytes = metadata_bytes(entry);
                if (entryBytes > effective.maxMetadataBytes - metadataBytes)
                {
                    return Result<WorkspaceSearchResult>::failure(make_io_error(
                        a_assertContext, IoError::CapacityExceeded, "Workspace search metadata limit was exceeded"));
                }
                metadataBytes += entryBytes;

                if (entry.type == WorkspaceEntryType::Directory && entry.is_operable())
                {
                    if (current.depth == effective.maxDepth)
                    {
                        return Result<WorkspaceSearchResult>::failure(make_io_error(
                            a_assertContext, IoError::CapacityExceeded, "Workspace search depth limit was exceeded"));
                    }
                    children.push_back(WorkspaceDirectory::from_bound_path(*entry.locator));
                }
                if (matches_filter(entry.displayName, a_filter))
                {
                    if (result.entries.size() == effective.maxResults)
                    {
                        return Result<WorkspaceSearchResult>::failure(make_io_error(
                            a_assertContext, IoError::CapacityExceeded, "Workspace search result limit was exceeded"));
                    }
                    result.entries.push_back(std::move(entry));
                }
            }

            for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator)
            {
                pending.push_back(PendingDirectory{std::move(*iterator), current.depth + 1U});
            }
        }
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }

    sort_workspace_entries(result.entries);
    return Result<WorkspaceSearchResult>::success(std::move(result));
}
} // namespace cue
