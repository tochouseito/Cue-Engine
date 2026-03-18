#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <vector>
#include <span>

// === Windows API include ===
#include "WinFile.h"

namespace Cue::PAL::Win
{
    class WinFileSystem final : public Core::IO::IFileSystem
    {
    public:
        WinFileSystem() = default;
        ~WinFileSystem() override = default;

        // --- 基本メタデータ ---
        Result exists(const Core::IO::Path& path, bool* out_exists) noexcept override;
        Result stat(const Core::IO::Path& path, Core::IO::FileStat* out_stat) noexcept override;

        // --- ディレクトリ操作 ---
        Result create_directories(const Core::IO::Path& path) noexcept override;
        Result list_directory(const Core::IO::Path& path, std::vector<Core::IO::Path>* out_entries) noexcept override;

        // --- 変更操作 ---
        Result remove(const Core::IO::Path& path, bool* out_removed) noexcept override;
        Result rename(const Core::IO::Path& from, const Core::IO::Path& to) noexcept override;
        Result path_to_native_w(const Core::IO::Path& path, std::wstring* out) noexcept;

        // --- ファイルI/O ---
        Result open(const Core::IO::Path& path, const Core::IO::FileOpenDesc& desc, std::unique_ptr<Core::IO::IFile>* out_file) noexcept override;

        // --- 便利関数 ---
        Result read_all(const Core::IO::Path& path, std::vector<std::byte>* out_data) noexcept override;
        Result write_all(const Core::IO::Path& path, std::span<const std::byte> data, bool create_parent_dirs) noexcept override;
    };
}
