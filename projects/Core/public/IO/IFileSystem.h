#pragma once

// === C++ Standard Library ===
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <span>

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include "Path.h"

namespace Cue::Core::IO
{
    enum class FileType : uint8_t
    {
        unknown = 0,
        regular,
        directory,
    };

    struct FileStat
    {
        FileType type = FileType::unknown;
        uint64_t size_bytes = 0;
        int64_t  mtime_ns = 0; // 最終更新 (ns)
    };

    enum class SeekOrigin : uint8_t
    {
        begin = 0,
        current,
        end,
    };

    enum class OpenAccess : uint8_t
    {
        read = 0,
        write,
        read_write,
    };

    enum class OpenCreate : uint8_t
    {
        open_existing = 0,  // 無ければ失敗
        open_always,        // 無ければ作成
        create_new,         // 既にあれば失敗
        create_always,      // 常に作成（既存は上書き）
        truncate_existing,  // 既存を切り詰め（無ければ失敗）
    };

    enum class OpenFlags : uint32_t
    {
        none = 0,
        append = 1u << 0, // 末尾追記（write時）
        sequential = 1u << 1, // OSヒント（最適化用）
        random = 1u << 2, // OSヒント（最適化用）
        no_buffer = 1u << 3, // 直I/O系（対応しない実装もOK）
    };

    [[nodiscard]] constexpr OpenFlags operator|(OpenFlags a, OpenFlags b) noexcept
    {
        return static_cast<OpenFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    [[nodiscard]] constexpr bool has_flag(OpenFlags v, OpenFlags f) noexcept
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0u;
    }

    struct FileOpenDesc
    {
        OpenAccess access = OpenAccess::read;
        OpenCreate create = OpenCreate::open_existing;
        OpenFlags  flags = OpenFlags::none;
    };

    class IFile
    {
    public:
        IFile() = default;
        virtual ~IFile() = default;

        IFile(const IFile&) = delete;
        IFile& operator=(const IFile&) = delete;
        IFile(IFile&&) = delete;
        IFile& operator=(IFile&&) = delete;

        virtual Result read(std::span<std::byte> dst, uint64_t* out_read) noexcept = 0;
        virtual Result write(std::span<const std::byte> src, uint64_t* out_written) noexcept = 0;

        virtual Result seek(int64_t offset, SeekOrigin origin) noexcept = 0;
        virtual Result tell(uint64_t* out_pos) noexcept = 0;
        virtual Result size(uint64_t* out_size) noexcept = 0;

        virtual Result flush() noexcept = 0;
        virtual Result close() noexcept = 0;
    };

    class IFileSystem
    {
    public:
        IFileSystem() = default;
        virtual ~IFileSystem() = default;

        IFileSystem(const IFileSystem&) = delete;
        IFileSystem& operator=(const IFileSystem&) = delete;
        IFileSystem(IFileSystem&&) = delete;
        IFileSystem& operator=(IFileSystem&&) = delete;

        // --- 基本メタデータ ---
        virtual Result exists(const Path& path, bool* out_exists) noexcept = 0;
        virtual Result stat(const Path& path, FileStat* out_stat) noexcept = 0;

        // --- ディレクトリ操作 ---
        virtual Result create_directories(const Path& path) noexcept = 0;
        virtual Result list_directory(const Path& path, std::vector<Path>* out_entries) noexcept = 0;

        // --- 変更操作 ---
        virtual Result remove(const Path& path, bool* out_removed) noexcept = 0;
        virtual Result rename(const Path& from, const Path& to) noexcept = 0;

        // --- ファイルI/O ---
        virtual Result open(const Path& path, const FileOpenDesc& desc, std::unique_ptr<IFile>* out_file) noexcept = 0;

        // --- 便利関数（必要なら実装側で最適化しても良い）---
        virtual Result read_all(const Path& path, std::vector<std::byte>* out_data) noexcept = 0;
        virtual Result write_all(const Path& path, std::span<const std::byte> data, bool create_parent_dirs) noexcept = 0;
    };
}
