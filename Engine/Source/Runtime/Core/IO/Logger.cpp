#include "Logger.h"

// === C++ includes ===
#include <cstddef>
#include <cstdio>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

#if defined(CUE_EDITOR) && defined(_WIN32)
// === Windows includes ===
#include <Windows.h>
#endif

namespace Cue::Core::IO
{
    namespace
    {
        struct LogFileState final
        {
            IFileSystem* fileSystem = nullptr;
            Path path{};
            std::mutex mutex{};
        };

        LogFileState& log_file_state() noexcept
        {
            // ログファイル設定をプロセス内で共有
            static LogFileState state{};
            return state;
        }

        std::span<const std::byte> as_bytes(std::string_view a_message) noexcept
        {
            // 文字列ビューを IFile 書き込み用のバイト列へ変換
            return std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(a_message.data()),
                a_message.size()
            };
        }
    }

    void out_debug_console([[maybe_unused]] std::string_view a_message)
    {
#if defined(CUE_EDITOR) && defined(_WIN32)
        // Editor Visual Studio の出力ウィンドウへ出力
        ::OutputDebugStringA(a_message.data());
#endif // defined(CUE_EDITOR) && defined(_WIN32)
    }

    Result set_log_file(IFileSystem& a_fileSystem, Path a_path, bool a_truncate) noexcept
    {
        if (a_path.is_empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Log file path must not be empty.");
        }

        if (a_truncate)
        {
            // 初回設定時に既存ログを空にする
            const std::span<const std::byte> empty{};
            Result result = a_fileSystem.write_all(a_path, empty, true);
            if (!result)
            {
                return result;
            }
        }

        LogFileState& state = log_file_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        // ファイルシステムは非所有で保持
        state.fileSystem = &a_fileSystem;
        state.path = std::move(a_path);
        return Result::ok();
    }

    void clear_log_file() noexcept
    {
        LogFileState& state = log_file_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        // 破棄済みファイルシステム参照を残さない
        state.fileSystem = nullptr;
        state.path = Path{};
    }

    void out_log_file(std::string_view a_message) noexcept
    {
        LogFileState& state = log_file_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.fileSystem == nullptr || state.path.is_empty())
        {
            return;
        }

        FileOpenDesc desc{};
        // ログは既存ファイルを保持して末尾へ追記
        desc.access = OpenAccess::write;
        desc.create = OpenCreate::open_always;
        desc.flags = OpenFlags::append | OpenFlags::sequential;

        std::unique_ptr<IFile> file{};
        // ログ出力失敗はアプリ本体へ伝播させない
        Result result = state.fileSystem->open(state.path, desc, &file);
        if (!result || !file)
        {
            return;
        }

        uint64_t written = 0;
        result = file->write(as_bytes(a_message), &written);
        if (result && written == static_cast<uint64_t>(a_message.size()))
        {
            // デバッグ用途のため書き込みごとに永続化
            result = file->flush();
            if (!result)
            {
                out_debug_console("Failed to flush log file.\n");
            }
        }
        result = file->close();
        if (!result)
        {
            out_debug_console("Failed to close log file.\n");
        }
    }
}
