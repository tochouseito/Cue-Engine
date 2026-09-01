#pragma once

#include <Cue/IO/Filesystem.h>

#include <memory>
#include <string_view>

namespace cue
{
class AssertContext;

/// @brief UTF-8 Absolute Path を検証し、Root 配下だけを操作する Windows Filesystem を生成する
///
/// @param a_rootPath 呼び出し中だけ参照する既存 Absolute Directory Path
/// @param a_assertContext 返却された Filesystem より長く生存する非所有診断 Context
/// @return 成功時は Root Native Handle を所有する Filesystem、失敗時は Portable 分類と Win32 診断
[[nodiscard]] Result<std::unique_ptr<FilesystemRoot>> create_windows_filesystem_root(
    std::string_view a_rootPath, const AssertContext &a_assertContext) noexcept;
} // namespace cue
