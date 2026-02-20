#pragma once
// === C++ Standard Library ===
#include <cstdint>
#include <memory>
#include <vector>
#include <span>

// === Core includes ===
#include <IO/IFileSystem.h>

namespace Cue::Platform::Win
{
    class WinFileSystem : public Core::IO::IFileSystem
    {
    public:
        WinFileSystem() = default;
        ~WinFileSystem() override = default;

        WinFileSystem(const WinFileSystem&) = delete;
        WinFileSystem& operator=(const WinFileSystem&) = delete;
        WinFileSystem(WinFileSystem&&) = delete;
        WinFileSystem& operator=(WinFileSystem&&) = delete;

        // --- 基本メタデータ ---
        Result exists(const Cue::Core::IO::Path& path, bool* out_exists) noexcept override;
        Result stat(const Cue::Core::IO::Path& path, Cue::Core::IO::FileStat* out_stat) noexcept override;

        // --- ディレクトリ操作 ---
        Result create_directories(const Cue::Core::IO::Path& path) noexcept override;
        Result list_directory(const Cue::Core::IO::Path& path, std::vector<Cue::Core::IO::Path>* out_entries) noexcept override;

        // --- 変更操作 ---
        Result remove(const Cue::Core::IO::Path& path, bool* out_removed) noexcept override;
        Result rename(const Cue::Core::IO::Path& from, const Cue::Core::IO::Path& to) noexcept override;

        // --- ファイルI/O ---
        Result open(const Cue::Core::IO::Path& path, const Cue::Core::IO::FileOpenDesc& desc, std::unique_ptr<Cue::Core::IO::IFile>* out_file) noexcept override;

        // --- 便利関数 ---
        Result read_all(const Cue::Core::IO::Path& path, std::vector<std::byte>* out_data) noexcept override;
        Result write_all(const Cue::Core::IO::Path& path, std::span<const std::byte> data, bool create_parent_dirs) noexcept override;
    };
}
