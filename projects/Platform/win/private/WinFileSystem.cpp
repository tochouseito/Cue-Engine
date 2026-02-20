#include "win_pch.h"
#include "WinFileSystem.h"

#include <string>
#include <string_view>

namespace
{
    using Result = Cue::Result;

    [[nodiscard]] static Result make_invalid_arg_error(std::string_view message) noexcept
    {
        return Result::fail(
            Cue::Facility::IO,
            Cue::Code::InvalidArg,
            Cue::Severity::Error,
            0,
            message);
    }

    [[nodiscard]] static Result make_io_error(std::string_view message) noexcept
    {
        return Result::fail(
            Cue::Facility::IO,
            Cue::Code::IoError,
            Cue::Severity::Error,
            0,
            message);
    }

    [[nodiscard]] static Result make_not_supported_error(std::string_view message) noexcept
    {
        return Result::fail(
            Cue::Facility::IO,
            Cue::Code::Unsupported,
            Cue::Severity::Error,
            0,
            message);
    }

    [[nodiscard]] static Result map_win_error(const DWORD e) noexcept
    {
        // 1) よくあるものを優先
        switch (e)
        {
        case ERROR_SUCCESS:
            return Result::ok();
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return Result::fail(
                Cue::Facility::Platform,
                Cue::Code::NotFound,
                Cue::Severity::Error,
                static_cast<uint32_t>(e),
                "Windows API returned NotFound.");
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return Result::fail(
                Cue::Facility::Platform,
                Cue::Code::AccessDenied,
                Cue::Severity::Error,
                static_cast<uint32_t>(e),
                "Windows API returned AccessDenied.");
        case ERROR_INVALID_PARAMETER:
        case ERROR_BAD_PATHNAME:
        case ERROR_DIRECTORY:
            return Result::fail(
                Cue::Facility::Platform,
                Cue::Code::InvalidArg,
                Cue::Severity::Error,
                static_cast<uint32_t>(e),
                "Windows API returned InvalidArg.");
        case ERROR_NOT_SUPPORTED:
            return Result::fail(
                Cue::Facility::Platform,
                Cue::Code::Unsupported,
                Cue::Severity::Error,
                static_cast<uint32_t>(e),
                "Windows API returned Unsupported.");
        default:
            return Result::fail(
                Cue::Facility::Platform,
                Cue::Code::Unknown,
                Cue::Severity::Error,
                static_cast<uint32_t>(e),
                "Windows API returned an unknown error.");
        }
    }

    [[nodiscard]] static Result utf8_to_wide(std::string_view s, std::wstring* out) noexcept
    {
        // 1) 引数チェック
        if (out == nullptr)
        {
            return make_invalid_arg_error("Output pointer is null.");
        }

        // 2) 空文字
        if (s.empty())
        {
            out->clear();
            return Result::ok();
        }

        // 3) 必要サイズ
        const int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0)
        {
            return map_win_error(::GetLastError());
        }

        // 4) 変換
        out->assign(static_cast<size_t>(needed), L'\0');
        const int written = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out->data(), needed);
        if (written != needed)
        {
            return map_win_error(::GetLastError());
        }

        return Result::ok();
    }

    [[nodiscard]] static Result wide_to_utf8(std::wstring_view s, std::string* out) noexcept
    {
        // 1) 引数チェック
        if (out == nullptr)
        {
            return make_invalid_arg_error("Output pointer is null.");
        }

        // 2) 空文字
        if (s.empty())
        {
            out->clear();
            return Result::ok();
        }

        // 3) 必要サイズ
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return map_win_error(::GetLastError());
        }

        // 4) 変換
        out->assign(static_cast<size_t>(needed), '\0');
        const int written = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out->data(), needed, nullptr, nullptr);
        if (written != needed)
        {
            return map_win_error(::GetLastError());
        }

        return Result::ok();
    }

    [[nodiscard]] static void to_native_seps(std::wstring* s) noexcept
    {
        // 1) nullptr ガード
        if (s == nullptr)
        {
            return;
        }

        // 2) '/' -> '\'
        for (wchar_t& c : *s)
        {
            if (c == L'/')
            {
                c = L'\\';
            }
        }

        // 3) UNC: 先頭が "//" なら "\\"
        if (s->size() >= 2 && (*s)[0] == L'\\' && (*s)[1] == L'\\')
        {
            return;
        }
        if (s->size() >= 2 && (*s)[0] == L'/' && (*s)[1] == L'/')
        {
            (*s)[0] = L'\\';
            (*s)[1] = L'\\';
        }
    }

    [[nodiscard]] static Result path_to_native_w(const Cue::Core::IO::Path& p, std::wstring* out) noexcept
    {
        // 1) UTF-8 -> UTF-16
        Result r = utf8_to_wide(p.utf8(), out);
        if (!r)
        {
            return r;
        }

        // 2) 区切りを Win に合わせる
        to_native_seps(out);

        return Result::ok();
    }

    [[nodiscard]] static int64_t filetime_to_unix_ns(const FILETIME ft) noexcept
    {
        // 1) FILETIME(100ns, 1601-01-01) -> uint64
        ULARGE_INTEGER u{};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;

        // 2) 1970-01-01 までの差（100ns単位）
        //    116444736000000000 = 1601->1970 の 100ns
        constexpr uint64_t kEpochDiff100ns = 116444736000000000ULL;
        if (u.QuadPart <= kEpochDiff100ns)
        {
            return 0;
        }

        const uint64_t unix100ns = u.QuadPart - kEpochDiff100ns;

        // 3) 100ns -> ns
        const uint64_t unixNs = unix100ns * 100ULL;
        if (unixNs > static_cast<uint64_t>(INT64_MAX))
        {
            return INT64_MAX;
        }

        return static_cast<int64_t>(unixNs);
    }

    [[nodiscard]] static Result get_attrs(const std::wstring& path, WIN32_FILE_ATTRIBUTE_DATA* out) noexcept
    {
        // 1) 引数チェック
        if (out == nullptr)
        {
            return make_invalid_arg_error("Output pointer is null.");
        }

        // 2) 取得
        if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, out) == FALSE)
        {
            return map_win_error(::GetLastError());
        }

        return Result::ok();
    }

    class WinFile final : public Cue::Core::IO::IFile
    {
    public:
        explicit WinFile(HANDLE h) noexcept
            : m_handle(h)
        {}

        ~WinFile() override
        {
            // 1) close を呼ばれていなくてもリークさせない
            if (m_handle != INVALID_HANDLE_VALUE)
            {
                (void)::CloseHandle(m_handle);
                m_handle = INVALID_HANDLE_VALUE;
            }
        }

        Result read(std::span<std::byte> dst, uint64_t* out_read) noexcept override
        {
            // 1) 引数チェック
            if (out_read == nullptr)
            {
                return make_invalid_arg_error("Invalid argument.");
            }
            *out_read = 0;

            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return make_invalid_arg_error("Invalid argument.");
            }

            // 2) 0バイト
            if (dst.size() == 0)
            {
                return Result::ok();
            }

            // 3) ReadFile (最大DWORDずつ)
            uint64_t total = 0;
            size_t offset = 0;

            while (offset < dst.size())
            {
                const size_t remain = dst.size() - offset;
                const DWORD chunk = (remain > static_cast<size_t>(MAXDWORD)) ? MAXDWORD : static_cast<DWORD>(remain);

                DWORD readBytes = 0;
                if (::ReadFile(m_handle, dst.data() + offset, chunk, &readBytes, nullptr) == FALSE)
                {
                    return map_win_error(::GetLastError());
                }

                total += static_cast<uint64_t>(readBytes);
                offset += static_cast<size_t>(readBytes);

                // 4) EOF
                if (readBytes == 0)
                {
                    break;
                }
            }

            *out_read = total;
            return Result::ok();
        }

        Result write(std::span<const std::byte> src, uint64_t* out_written) noexcept override
        {
            // 1) 引数チェック
            if (out_written == nullptr)
            {
                return make_invalid_arg_error("Invalid argument.");
            }
            *out_written = 0;

            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return make_invalid_arg_error("Invalid argument.");
            }

            // 2) 0バイト
            if (src.size() == 0)
            {
                return Result::ok();
            }

            // 3) WriteFile (最大DWORDずつ)
            uint64_t total = 0;
            size_t offset = 0;

            while (offset < src.size())
            {
                const size_t remain = src.size() - offset;
                const DWORD chunk = (remain > static_cast<size_t>(MAXDWORD)) ? MAXDWORD : static_cast<DWORD>(remain);

                DWORD written = 0;
                if (::WriteFile(m_handle, src.data() + offset, chunk, &written, nullptr) == FALSE)
                {
                    return map_win_error(::GetLastError());
                }

                total += static_cast<uint64_t>(written);
                offset += static_cast<size_t>(written);

                // 4) 異常（書けない）
                if (written == 0)
                {
                    return make_io_error("I/O operation failed.");
                }
            }

            *out_written = total;
            return Result::ok();
        }

        Result seek(int64_t offset, Cue::Core::IO::SeekOrigin origin) noexcept override
        {
            // 1) ハンドルチェック
            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return make_invalid_arg_error("Invalid argument.");
            }

            // 2) origin
            DWORD moveMethod = FILE_BEGIN;
            switch (origin)
            {
            case Cue::Core::IO::SeekOrigin::begin:
                moveMethod = FILE_BEGIN;
                break;
            case Cue::Core::IO::SeekOrigin::current:
                moveMethod = FILE_CURRENT;
                break;
            case Cue::Core::IO::SeekOrigin::end:
                moveMethod = FILE_END;
                break;
            default:
                return make_invalid_arg_error("Invalid argument.");
            }

            // 3) SetFilePointerEx
            LARGE_INTEGER li{};
            li.QuadPart = offset;

            LARGE_INTEGER newPos{};
            if (::SetFilePointerEx(m_handle, li, &newPos, moveMethod) == FALSE)
            {
                return map_win_error(::GetLastError());
            }

            return Result::ok();
        }

        Result tell(uint64_t* out_pos) noexcept override
        {
            // 1) 引数チェック
            if (out_pos == nullptr)
            {
                return make_invalid_arg_error("Invalid argument.");
            }
            *out_pos = 0;

            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return make_invalid_arg_error("Invalid argument.");
            }

            // 2) 現在位置 = seek(0, current)
            LARGE_INTEGER zero{};
            zero.QuadPart = 0;

            LARGE_INTEGER newPos{};
            if (::SetFilePointerEx(m_handle, zero, &newPos, FILE_CURRENT) == FALSE)
            {
                return map_win_error(::GetLastError());
            }

            if (newPos.QuadPart < 0)
            {
                return make_io_error("I/O operation failed.");
            }

            *out_pos = static_cast<uint64_t>(newPos.QuadPart);
            return Result::ok();
        }

        Result size(uint64_t* out_size) noexcept override
        {
            // 1) 引数チェック
            if (out_size == nullptr)
            {
                return make_invalid_arg_error("Invalid argument.");
            }
            *out_size = 0;

            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return make_invalid_arg_error("Invalid argument.");
            }

            // 2) GetFileSizeEx
            LARGE_INTEGER sz{};
            if (::GetFileSizeEx(m_handle, &sz) == FALSE)
            {
                return map_win_error(::GetLastError());
            }

            if (sz.QuadPart < 0)
            {
                return make_io_error("I/O operation failed.");
            }

            *out_size = static_cast<uint64_t>(sz.QuadPart);
            return Result::ok();
        }

        Result flush() noexcept override
        {
            // 1) ハンドルチェック
            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return make_invalid_arg_error("Invalid argument.");
            }

            // 2) FlushFileBuffers
            if (::FlushFileBuffers(m_handle) == FALSE)
            {
                return map_win_error(::GetLastError());
            }

            return Result::ok();
        }

        Result close() noexcept override
        {
            // 1) 既に閉じている
            if (m_handle == INVALID_HANDLE_VALUE)
            {
                return Result::ok();
            }

            // 2) CloseHandle
            if (::CloseHandle(m_handle) == FALSE)
            {
                return map_win_error(::GetLastError());
            }

            m_handle = INVALID_HANDLE_VALUE;
            return Result::ok();
        }

    private:
        HANDLE m_handle = INVALID_HANDLE_VALUE;
    };

    [[nodiscard]] static Result build_find_pattern(const std::wstring& dir, std::wstring* outPattern) noexcept
    {
        // 1) 引数チェック
        if (outPattern == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }

        // 2) 末尾に "\*" を付ける
        *outPattern = dir;

        if (!outPattern->empty() && outPattern->back() != L'\\')
        {
            outPattern->push_back(L'\\');
        }
        outPattern->push_back(L'*');

        return Result::ok();
    }

    [[nodiscard]] static Result create_dir_if_needed(const std::wstring& p) noexcept
    {
        // 1) CreateDirectoryW
        if (::CreateDirectoryW(p.c_str(), nullptr) != FALSE)
        {
            return Result::ok();
        }

        // 2) 既存は成功扱い
        const DWORD e = ::GetLastError();
        if (e == ERROR_ALREADY_EXISTS)
        {
            return Result::ok();
        }

        return map_win_error(e);
    }

    [[nodiscard]] static Result split_unc_root(std::wstring_view s, size_t* outRootLen) noexcept
    {
        // 1) //server/share/...
        if (outRootLen == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        *outRootLen = 0;

        // 2) 先頭 "\\"
        if (s.size() < 2 || s[0] != L'\\' || s[1] != L'\\')
        {
            return make_invalid_arg_error("Invalid argument.");
        }

        // 3) \\server\share\ までを root にする
        size_t i = 2;

        // server
        while (i < s.size() && s[i] != L'\\')
        {
            ++i;
        }
        if (i >= s.size())
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        ++i;

        // share
        while (i < s.size() && s[i] != L'\\')
        {
            ++i;
        }

        // share 末尾の "\" まで含める（あれば）
        if (i < s.size() && s[i] == L'\\')
        {
            ++i;
        }

        *outRootLen = i;
        return Result::ok();
    }
}

namespace Cue::Platform::Win
{
    Result WinFileSystem::exists(const Cue::Core::IO::Path& path, bool* out_exists) noexcept
    {
        // 1) 引数チェック
        if (out_exists == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        *out_exists = false;

        // 2) 変換
        std::wstring wpath{};
        Result r = path_to_native_w(path, &wpath);
        if (!r)
        {
            return r;
        }

        // 3) GetFileAttributesW
        const DWORD attrs = ::GetFileAttributesW(wpath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            {
                *out_exists = false;
                return Result::ok();
            }
            return map_win_error(e);
        }

        *out_exists = true;
        return Result::ok();
    }

    Result WinFileSystem::stat(const Cue::Core::IO::Path& path, Cue::Core::IO::FileStat* out_stat) noexcept
    {
        // 1) 引数チェック
        if (out_stat == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        *out_stat = {};

        // 2) 変換
        std::wstring wpath{};
        Result r = path_to_native_w(path, &wpath);
        if (!r)
        {
            return r;
        }

        // 3) 属性取得
        WIN32_FILE_ATTRIBUTE_DATA data{};
        r = get_attrs(wpath, &data);
        if (!r)
        {
            return r;
        }

        // 4) 種別
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
        {
            out_stat->type = Cue::Core::IO::FileType::directory;
            out_stat->size_bytes = 0;
        }
        else
        {
            out_stat->type = Cue::Core::IO::FileType::regular;
            ULARGE_INTEGER sz{};
            sz.LowPart = data.nFileSizeLow;
            sz.HighPart = data.nFileSizeHigh;
            out_stat->size_bytes = sz.QuadPart;
        }

        // 5) mtime
        out_stat->mtime_ns = filetime_to_unix_ns(data.ftLastWriteTime);

        return Result::ok();
    }

    Result WinFileSystem::create_directories(const Cue::Core::IO::Path& path) noexcept
    {
        // 1) 変換
        std::wstring wpath{};
        Result r = path_to_native_w(path, &wpath);
        if (!r)
        {
            return r;
        }

        // 2) 空は何もしない
        if (wpath.empty())
        {
            return Result::ok();
        }

        // 3) ルートを決める（C:\ / \ / \\server\share\）
        size_t i = 0;

        if (wpath.size() >= 3 && ((wpath[1] == L':') && (wpath[2] == L'\\')))
        {
            i = 3; // "C:\"
        }
        else if (wpath.size() >= 2 && (wpath[0] == L'\\' && wpath[1] == L'\\'))
        {
            size_t rootLen = 0;
            r = split_unc_root(std::wstring_view{ wpath }, &rootLen);
            if (!r)
            {
                return r;
            }
            i = rootLen;
        }
        else if (!wpath.empty() && wpath[0] == L'\\')
        {
            i = 1; // "\"
        }
        else
        {
            i = 0; // relative
        }

        // 4) パスを走査して段階的に作成
        std::wstring cur = wpath.substr(0, i);

        while (i < wpath.size())
        {
            // 4-1) 区切りをスキップ
            while (i < wpath.size() && wpath[i] == L'\\')
            {
                ++i;
            }
            if (i >= wpath.size())
            {
                break;
            }

            // 4-2) 次のセグメント
            const size_t start = i;
            while (i < wpath.size() && wpath[i] != L'\\')
            {
                ++i;
            }

            const std::wstring_view seg = std::wstring_view{ wpath }.substr(start, i - start);
            if (seg.empty())
            {
                continue;
            }

            // 4-3) cur に追加
            if (!cur.empty() && cur.back() != L'\\')
            {
                cur.push_back(L'\\');
            }
            cur.append(seg.data(), seg.size());

            // 4-4) 作成
            r = create_dir_if_needed(cur);
            if (!r)
            {
                return r;
            }
        }

        return Result::ok();
    }

    Result WinFileSystem::list_directory(const Cue::Core::IO::Path& path, std::vector<Cue::Core::IO::Path>* out_entries) noexcept
    {
        // 1) 引数チェック
        if (out_entries == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        out_entries->clear();

        // 2) native path
        std::wstring wdir{};
        Result r = path_to_native_w(path, &wdir);
        if (!r)
        {
            return r;
        }

        // 3) パターン作成
        std::wstring pattern{};
        r = build_find_pattern(wdir, &pattern);
        if (!r)
        {
            return r;
        }

        // 4) FindFirst/Next
        WIN32_FIND_DATAW fd{};
        HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            return map_win_error(::GetLastError());
        }

        // 5) ループ
        for (;;)
        {
            // 5-1) "." ".." を除外
            if (fd.cFileName[0] != L'\0')
            {
                const bool isDot = (wcscmp(fd.cFileName, L".") == 0);
                const bool isDotDot = (wcscmp(fd.cFileName, L"..") == 0);
                if (!isDot && !isDotDot)
                {
                    // 5-2) full path を作る
                    std::wstring wfull = wdir;
                    if (!wfull.empty() && wfull.back() != L'\\')
                    {
                        wfull.push_back(L'\\');
                    }
                    wfull += fd.cFileName;

                    // 5-3) wide -> utf8
                    std::string utf8{};
                    r = wide_to_utf8(wfull, &utf8);
                    if (!r)
                    {
                        (void)::FindClose(hFind);
                        return r;
                    }

                    // 5-4) "\" -> "/" に戻す（Pathの内部規約）
                    for (char& c : utf8)
                    {
                        if (c == '\\')
                        {
                            c = '/';
                        }
                    }

                    out_entries->push_back(Cue::Core::IO::Path{ std::move(utf8) }.normalize());
                }
            }

            // 5-5) 次
            if (::FindNextFileW(hFind, &fd) == FALSE)
            {
                const DWORD e = ::GetLastError();
                (void)::FindClose(hFind);

                if (e == ERROR_NO_MORE_FILES)
                {
                    return Result::ok();
                }
                return map_win_error(e);
            }
        }
    }

    Result WinFileSystem::remove(const Cue::Core::IO::Path& path, bool* out_removed) noexcept
    {
        // 1) 引数チェック
        if (out_removed == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        *out_removed = false;

        // 2) native
        std::wstring wpath{};
        Result r = path_to_native_w(path, &wpath);
        if (!r)
        {
            return r;
        }

        // 3) 属性確認
        const DWORD attrs = ::GetFileAttributesW(wpath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            {
                *out_removed = false;
                return Result::ok();
            }
            return map_win_error(e);
        }

        // 4) ディレクトリ or ファイル
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u)
        {
            if (::RemoveDirectoryW(wpath.c_str()) == FALSE)
            {
                return map_win_error(::GetLastError());
            }
        }
        else
        {
            if (::DeleteFileW(wpath.c_str()) == FALSE)
            {
                return map_win_error(::GetLastError());
            }
        }

        *out_removed = true;
        return Result::ok();
    }

    Result WinFileSystem::rename(const Cue::Core::IO::Path& from, const Cue::Core::IO::Path& to) noexcept
    {
        // 1) 変換
        std::wstring wfrom{};
        std::wstring wto{};

        Result r = path_to_native_w(from, &wfrom);
        if (!r)
        {
            return r;
        }

        r = path_to_native_w(to, &wto);
        if (!r)
        {
            return r;
        }

        // 2) 置換ありで MoveFileExW（write-temp -> atomic replace に使う）
        const DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED;
        if (::MoveFileExW(wfrom.c_str(), wto.c_str(), flags) == FALSE)
        {
            return map_win_error(::GetLastError());
        }

        return Result::ok();
    }

    Result WinFileSystem::open(const Cue::Core::IO::Path& path, const Cue::Core::IO::FileOpenDesc& desc, std::unique_ptr<Cue::Core::IO::IFile>* out_file) noexcept
    {
        // 1) 引数チェック
        if (out_file == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        out_file->reset();

        // 2) no_buffer は地雷なので未対応にする（整列/セクタサイズ問題が出る）
        if (Cue::Core::IO::has_flag(desc.flags, Cue::Core::IO::OpenFlags::no_buffer))
        {
            return make_not_supported_error("Operation is not supported.");
        }

        // 3) 変換
        std::wstring wpath{};
        Result r = path_to_native_w(path, &wpath);
        if (!r)
        {
            return r;
        }

        // 4) DesiredAccess
        DWORD desired = 0;
        switch (desc.access)
        {
        case Cue::Core::IO::OpenAccess::read:
            desired = GENERIC_READ;
            break;
        case Cue::Core::IO::OpenAccess::write:
            desired = GENERIC_WRITE;
            break;
        case Cue::Core::IO::OpenAccess::read_write:
            desired = GENERIC_READ | GENERIC_WRITE;
            break;
        default:
            return make_invalid_arg_error("Invalid argument.");
        }

        // 5) append
        if (Cue::Core::IO::has_flag(desc.flags, Cue::Core::IO::OpenFlags::append))
        {
            // 5-1) 書き込み時は FILE_APPEND_DATA を足す（GENERIC_WRITE でも動くが意図が明確）
            desired |= FILE_APPEND_DATA;
        }

        // 6) CreationDisposition
        DWORD disposition = OPEN_EXISTING;
        switch (desc.create)
        {
        case Cue::Core::IO::OpenCreate::open_existing:
            disposition = OPEN_EXISTING;
            break;
        case Cue::Core::IO::OpenCreate::open_always:
            disposition = OPEN_ALWAYS;
            break;
        case Cue::Core::IO::OpenCreate::create_new:
            disposition = CREATE_NEW;
            break;
        case Cue::Core::IO::OpenCreate::create_always:
            disposition = CREATE_ALWAYS;
            break;
        case Cue::Core::IO::OpenCreate::truncate_existing:
            disposition = TRUNCATE_EXISTING;
            break;
        default:
            return make_invalid_arg_error("Invalid argument.");
        }

        // 7) FlagsAndAttributes（ヒント）
        DWORD flags = FILE_ATTRIBUTE_NORMAL;
        if (Cue::Core::IO::has_flag(desc.flags, Cue::Core::IO::OpenFlags::sequential))
        {
            flags |= FILE_FLAG_SEQUENTIAL_SCAN;
        }
        if (Cue::Core::IO::has_flag(desc.flags, Cue::Core::IO::OpenFlags::random))
        {
            flags |= FILE_FLAG_RANDOM_ACCESS;
        }

        // 8) Share（Editor運用で重要）
        const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

        // 9) CreateFileW
        HANDLE h = ::CreateFileW(
            wpath.c_str(),
            desired,
            share,
            nullptr,
            disposition,
            flags,
            nullptr);

        if (h == INVALID_HANDLE_VALUE)
        {
            return map_win_error(::GetLastError());
        }

        // 10) append の場合、末尾へ
        if (Cue::Core::IO::has_flag(desc.flags, Cue::Core::IO::OpenFlags::append))
        {
            LARGE_INTEGER zero{};
            zero.QuadPart = 0;

            LARGE_INTEGER newPos{};
            if (::SetFilePointerEx(h, zero, &newPos, FILE_END) == FALSE)
            {
                const DWORD e = ::GetLastError();
                (void)::CloseHandle(h);
                return map_win_error(e);
            }
        }

        // 11) wrap
        *out_file = std::unique_ptr<Cue::Core::IO::IFile>(new WinFile(h));
        return Result::ok();
    }

    Result WinFileSystem::read_all(const Cue::Core::IO::Path& path, std::vector<std::byte>* out_data) noexcept
    {
        // 1) 引数チェック
        if (out_data == nullptr)
        {
            return make_invalid_arg_error("Invalid argument.");
        }
        out_data->clear();

        // 2) open
        Cue::Core::IO::FileOpenDesc d{};
        d.access = Cue::Core::IO::OpenAccess::read;
        d.create = Cue::Core::IO::OpenCreate::open_existing;
        d.flags = Cue::Core::IO::OpenFlags::sequential;

        std::unique_ptr<Cue::Core::IO::IFile> f{};
        Result r = open(path, d, &f);
        if (!r)
        {
            return r;
        }

        // 3) size
        uint64_t sz = 0;
        r = f->size(&sz);
        if (!r)
        {
            (void)f->close();
            return r;
        }

        if (sz > static_cast<uint64_t>(SIZE_MAX))
        {
            (void)f->close();
            return make_io_error("I/O operation failed.");
        }

        // 4) 読み込み
        out_data->resize(static_cast<size_t>(sz));

        uint64_t got = 0;
        r = f->read(std::span<std::byte>{ out_data->data(), out_data->size() }, & got);
        if (!r)
        {
            (void)f->close();
            out_data->clear();
            return r;
        }

        // 5) 期待サイズ未満なら詰める
        if (got < static_cast<uint64_t>(out_data->size()))
        {
            out_data->resize(static_cast<size_t>(got));
        }

        (void)f->close();
        return Result::ok();
    }

    Result WinFileSystem::write_all(const Cue::Core::IO::Path& path, std::span<const std::byte> data, bool create_parent_dirs) noexcept
    {
        // 1) 親ディレクトリ作成
        if (create_parent_dirs)
        {
            const Cue::Core::IO::Path parent = path.parent();
            if (!parent.is_empty())
            {
                const Result r0 = create_directories(parent);
                if (!r0)
                {
                    return r0;
                }
            }
        }

        // 2) open (create_always)
        Cue::Core::IO::FileOpenDesc d{};
        d.access = Cue::Core::IO::OpenAccess::write;
        d.create = Cue::Core::IO::OpenCreate::create_always;
        d.flags = Cue::Core::IO::OpenFlags::sequential;

        std::unique_ptr<Cue::Core::IO::IFile> f{};
        Result r = open(path, d, &f);
        if (!r)
        {
            return r;
        }

        // 3) write
        uint64_t written = 0;
        r = f->write(data, &written);
        if (!r)
        {
            (void)f->close();
            return r;
        }

        if (written != static_cast<uint64_t>(data.size()))
        {
            (void)f->close();
            return make_io_error("I/O operation failed.");
        }

        // 4) flush
        r = f->flush();
        if (!r)
        {
            (void)f->close();
            return r;
        }

        (void)f->close();
        return Result::ok();
    }
}


