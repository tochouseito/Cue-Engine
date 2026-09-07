#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/IO/WorkspaceFilesystem.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
class AssertContext;
}

namespace cue::project_files_private
{
/// @brief Project-local Trash Recordの永続状態
enum class TrashRecordState : std::uint8_t
{
    Allocating,
    Prepared,
    Trashed,
    Restoring,
    Restored,
    Aborting,
    Aborted
};

/// @brief Record Parserへ適用する縮小可能なResource上限
struct TrashRecordParseLimits final
{
    std::size_t maxBytes = 0U;
    std::size_t maxDepth = 0U;
    std::size_t maxStringBytes = 0U;
    std::size_t maxArrayElements = 0U;
    std::size_t maxObjectMembers = 0U;
    std::size_t maxValues = 0U;

    /// @brief 全Resource上限が非Zeroか判定する
    [[nodiscard]] bool is_valid() const noexcept;
};

/// @brief schemaVersion 1のHard Limitを返す
[[nodiscard]] TrashRecordParseLimits trash_record_hard_limits() noexcept;

/// @brief Trash Recordの共通Metadataと内容Fingerprintを所有する
struct TrashRecord final
{
    std::string projectId;
    std::string operationId;
    TrashRecordState state = TrashRecordState::Allocating;
    std::string originalPath;
    WorkspaceEntryFingerprint fingerprint;
};

/// @brief 決定済み順序のUTF-8 JSONへTrash Recordを直列化する
[[nodiscard]] Result<std::vector<std::byte>> serialize_trash_record(const TrashRecord &a_record,
                                                                    const AssertContext &a_assertContext) noexcept;

/// @brief Strict schemaVersion 1 JSONを上限内でTrash Recordへ変換する
[[nodiscard]] Result<TrashRecord> parse_trash_record(std::span<const std::byte> a_bytes,
                                                     TrashRecordParseLimits a_limits,
                                                     const AssertContext &a_assertContext) noexcept;

/// @brief 永続状態をschemaVersion 1の文字列へ変換する
[[nodiscard]] std::string_view trash_record_state_name(TrashRecordState a_state) noexcept;
} // namespace cue::project_files_private
