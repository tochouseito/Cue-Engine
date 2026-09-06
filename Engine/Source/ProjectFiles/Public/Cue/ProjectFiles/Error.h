#pragma once

#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;
}

namespace cue::project_files
{
/// @brief Project Workspace File操作の回復可能な失敗分類
enum class ProjectFileError : std::int64_t
{
    InvalidRequest = 1,
    ProtectedEntry = 2,
    InUse = 3,
    Conflict = 4,
    LimitExceeded = 5,
    RecoveryRequired = 6,
    Busy = 7,
    StorageFailure = 8
};

/// @brief Project File Errorを診断Summaryと共に生成する
[[nodiscard]] Error make_project_file_error(const AssertContext &a_assertContext, ProjectFileError a_code,
                                            std::string_view a_summary) noexcept;

/// @brief 下位Moduleの失敗をProject File Errorへ再分類する
[[nodiscard]] Error reclassify_project_file_error(const AssertContext &a_assertContext, ProjectFileError a_code,
                                                  std::string_view a_summary, Error &&a_cause) noexcept;
} // namespace cue::project_files
