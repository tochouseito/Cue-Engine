#pragma once

#include <Cue/IO/Filesystem.h>
#include <Cue/IO/RelativePath.h>

#include <memory>
#include <string_view>

namespace cue
{
class AssertContext;

/// @brief Windows Shellが管理するRoot Location分類
enum class WindowsKnownFolder
{
    LocalApplicationData,
};

/// @brief Known Folder配下のRelative Rootを開く方法
enum class WindowsRootOpenMode
{
    OpenExisting,
    CreateOrOpen,
};

/// @brief UTF-8 Absolute Path を検証し、Root 配下だけを操作する Windows Filesystem を生成する
///
/// @param a_rootPath 呼び出し中だけ参照する既存 Absolute Directory Path
/// @param a_assertContext 返却された Filesystem より長く生存する非所有診断 Context
/// @return 成功時は Root Native Handle を所有する Filesystem、失敗時は Portable 分類と Win32 診断
[[nodiscard]] Result<std::unique_ptr<FilesystemRoot>> create_windows_filesystem_root(
    std::string_view a_rootPath, const AssertContext &a_assertContext) noexcept;

/// @brief Windows Known Folder配下の検証済みRelative Rootを開くか作成してFilesystemを生成する
///
/// Known Folder自体は作成せず、CreateOrOpenではa_relativeRootの各Segmentだけを順に作成する
[[nodiscard]] Result<std::unique_ptr<FilesystemRoot>> create_windows_known_folder_filesystem_root(
    WindowsKnownFolder a_folder, const RelativePath &a_relativeRoot, WindowsRootOpenMode a_mode,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
