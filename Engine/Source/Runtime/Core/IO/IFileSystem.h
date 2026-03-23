#pragma once

// === C++ includes ===
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

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
        sequential = 1u << 1, // os ヒント
        random = 1u << 2, // os ヒント
        no_buffer = 1u << 3, // 直 i/o 系
    };

    [[nodiscard]] constexpr OpenFlags operator|(OpenFlags a_left, OpenFlags a_right) noexcept
    {
        return static_cast<OpenFlags>(static_cast<uint32_t>(a_left) | static_cast<uint32_t>(a_right));
    }

    [[nodiscard]] constexpr bool has_flag(OpenFlags a_value, OpenFlags a_flag) noexcept
    {
        return (static_cast<uint32_t>(a_value) & static_cast<uint32_t>(a_flag)) != 0u;
    }

    struct FileOpenDesc
    {
        OpenAccess access = OpenAccess::read;
        OpenCreate create = OpenCreate::open_existing;
        OpenFlags  flags = OpenFlags::none;
    };

    /// @brief 単一ファイルの入出力インターフェースです。
    class IFile
    {
    public:
        IFile() = default;
        virtual ~IFile() = default;

        IFile(const IFile&) = delete;
        IFile& operator=(const IFile&) = delete;
        IFile(IFile&&) = delete;
        IFile& operator=(IFile&&) = delete;

        /// @brief ファイルからバイト列を読み込みます。
        virtual Result read(std::span<std::byte> a_destination, uint64_t* a_outRead) noexcept = 0;
        /// @brief ファイルへバイト列を書き込みます。
        virtual Result write(std::span<const std::byte> a_source, uint64_t* a_outWritten) noexcept = 0;

        /// @brief ファイル位置を移動します。
        virtual Result seek(int64_t a_offset, SeekOrigin a_origin) noexcept = 0;
        /// @brief 現在位置を取得します。
        virtual Result tell(uint64_t* a_outPosition) noexcept = 0;
        /// @brief ファイルサイズを取得します。
        virtual Result size(uint64_t* a_outSize) noexcept = 0;

        /// @brief バッファ済みデータを永続化します。
        virtual Result flush() noexcept = 0;
        /// @brief ファイルを閉じます。
        virtual Result close() noexcept = 0;
    };

    /// @brief ファイルシステム操作を抽象化するインターフェースです。
    class IFileSystem
    {
    public:
        IFileSystem() = default;
        virtual ~IFileSystem() = default;
        // コピー禁止
        IFileSystem(const IFileSystem&) = delete;
        IFileSystem& operator=(const IFileSystem&) = delete;
        // ムーブ禁止
        IFileSystem(IFileSystem&&) = delete;
        IFileSystem& operator=(IFileSystem&&) = delete;

        // --- 基本メタデータ ---
        /// @brief パスの存在有無を取得します。
        virtual Result exists(const Path& a_path, bool* a_outExists) noexcept = 0;
        /// @brief ファイル情報を取得します。
        virtual Result stat(const Path& a_path, FileStat* a_outStat) noexcept = 0;

        // --- ディレクトリ操作 ---
        /// @brief ディレクトリを再帰的に作成します。
        virtual Result create_directories(const Path& a_path) noexcept = 0;
        /// @brief ディレクトリ内容を列挙します。
        virtual Result list_directory(const Path& a_path, std::vector<Path>* a_outEntries) noexcept = 0;

        // --- 変更操作 ---
        /// @brief ファイルまたはディレクトリを削除します。
        virtual Result remove(const Path& a_path, bool* a_outRemoved) noexcept = 0;
        /// @brief パスをリネームします。
        virtual Result rename(const Path& a_from, const Path& a_to) noexcept = 0;

        // --- ファイルI/O ---
        /// @brief ファイルを開きます。
        virtual Result open(const Path& a_path, const FileOpenDesc& a_desc, std::unique_ptr<IFile>* a_outFile) noexcept = 0;

        // --- 便利関数（必要なら実装側で最適化しても良い）---
        /// @brief ファイル全体を読み込みます。
        virtual Result read_all(const Path& a_path, std::vector<std::byte>* a_outData) noexcept = 0;
        /// @brief ファイル全体を書き込みます。
        virtual Result write_all(const Path& a_path, std::span<const std::byte> a_data, bool a_createParentDirs) noexcept = 0;
    };
} // 名前空間 cue::core::io
