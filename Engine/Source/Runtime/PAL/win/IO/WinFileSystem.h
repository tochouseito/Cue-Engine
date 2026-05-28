// WinFileSystem の役割と公開要素を定義する

#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <span>
#include <string>

// === Windows API includes ===
#include "WinFile.h"

namespace Cue::PAL::Win
{
    /// @brief Windows ファイルシステム実装
    class WinFileSystem final : public Core::IO::IFileSystem
    {
    public:
        WinFileSystem() = default;
        ~WinFileSystem() override = default;

        // --- 基本メタデータ ---
        Result executable_directory(Core::IO::Path& a_outDirectory) noexcept override;
        Result exists(const Core::IO::Path& a_path, bool* a_outExists) noexcept override;
        Result stat(const Core::IO::Path& a_path, Core::IO::FileStat* a_outStat) noexcept override;

        // --- ディレクトリ操作 ---
        Result create_directories(const Core::IO::Path& a_path) noexcept override;
        Result list_directory(const Core::IO::Path& a_path, std::vector<Core::IO::Path>* a_outEntries) noexcept override;

        // --- 変更操作 ---
        Result remove(const Core::IO::Path& a_path, bool* a_outRemoved) noexcept override;
        Result rename(const Core::IO::Path& a_from, const Core::IO::Path& a_to) noexcept override;
        Result copy_file(
            const Core::IO::Path& a_from,
            const Core::IO::Path& a_to,
            bool a_overwrite) noexcept override;
        Result path_to_native_w(const Core::IO::Path& a_path, std::wstring* a_outText) noexcept;

        // --- ファイルI/O ---
        Result open(const Core::IO::Path& a_path, const Core::IO::FileOpenDesc& a_desc, std::unique_ptr<Core::IO::IFile>* a_outFile) noexcept override;

        // --- 便利関数 ---
        Result read_all(const Core::IO::Path& a_path, std::vector<std::byte>* a_outData) noexcept override;
        Result write_all(const Core::IO::Path& a_path, std::span<const std::byte> a_data, bool a_createParentDirs) noexcept override;
    };
}
