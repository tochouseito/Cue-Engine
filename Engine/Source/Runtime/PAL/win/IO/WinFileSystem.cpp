#include "WinFileSystem.h"
#include "ConvertUTF.h"
#include "ConvertHresult.h"

namespace Cue::PAL::Win
{
    [[nodiscard]] static void to_native_seps(std::wstring* a_text) noexcept
    {
        // - nullptr ガード
        if (a_text == nullptr)
        {
            return;
        }

        // - '/' -> '\'
        for (wchar_t& c : *a_text)
        {
            if (c == L'/')
            {
                c = L'\\';
            }
        }

        // - UNC: 先頭が "//" なら "\\"
        if (a_text->size() >= 2 && (*a_text)[0] == L'\\' && (*a_text)[1] == L'\\')
        {
            return;
        }
        if (a_text->size() >= 2 && (*a_text)[0] == L'/' && (*a_text)[1] == L'/')
        {
            (*a_text)[0] = L'\\';
            (*a_text)[1] = L'\\';
        }
    }

    [[nodiscard]] static int64_t filetime_to_unix_ns(const FILETIME a_fileTime) noexcept
    {
        // - FILETIME(100ns, 1601-01-01) -> uint64
        ULARGE_INTEGER u{};
        u.LowPart = a_fileTime.dwLowDateTime;
        u.HighPart = a_fileTime.dwHighDateTime;

        // - 1970-01-01 までの差（100ns単位）
        //    116444736000000000 = 1601->1970 の 100ns
        constexpr uint64_t kEpochDiff100ns = 116444736000000000ULL;
        if (u.QuadPart <= kEpochDiff100ns)
        {
            return 0;
        }

        const uint64_t unix100ns = u.QuadPart - kEpochDiff100ns;

        // - 100ns -> ns
        const uint64_t unixNs = unix100ns * 100ULL;
        if (unixNs > static_cast<uint64_t>(INT64_MAX))
        {
            return INT64_MAX;
        }

        return static_cast<int64_t>(unixNs);
    }

    [[nodiscard]] static Result get_attrs(const std::wstring& a_path, WIN32_FILE_ATTRIBUTE_DATA* a_outData) noexcept
    {
        // - 引数チェック
        if (a_outData == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }

        // - 取得
        if (::GetFileAttributesExW(a_path.c_str(), GetFileExInfoStandard, a_outData) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to get file attributes.");
        }

        return Result::ok();
    }

    [[nodiscard]] static Result build_find_pattern(const std::wstring& a_directory, std::wstring* a_outPattern) noexcept
    {
        // - 引数チェック
        if (a_outPattern == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }

        // - 末尾に "\*" を付ける
        *a_outPattern = a_directory;

        if (!a_outPattern->empty() && a_outPattern->back() != L'\\')
        {
            a_outPattern->push_back(L'\\');
        }
        a_outPattern->push_back(L'*');

        return Result::ok();
    }

    [[nodiscard]] static Result create_dir_if_needed(const std::wstring& a_path) noexcept
    {
        // - CreateDirectoryW
        if (::CreateDirectoryW(a_path.c_str(), nullptr) != FALSE)
        {
            return Result::ok();
        }

        // - 既存は成功扱い
        const DWORD e = ::GetLastError();
        if (e == ERROR_ALREADY_EXISTS)
        {
            return Result::ok();
        }

        return Result::fail(
            convert_hresult_code(HRESULT_FROM_WIN32(e)), Severity::Error,
            "Failed to create directory.");
    }

    [[nodiscard]] static Result split_unc_root(std::wstring_view a_text, size_t* a_outRootLength) noexcept
    {
        // - //server/share/...
        if (a_outRootLength == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outRootLength = 0;

        // - 先頭 "\\"
        if (a_text.size() < 2 || a_text[0] != L'\\' || a_text[1] != L'\\')
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Path is not a valid UNC path.");
        }

        // - \\server\share\ までを root にする
        size_t i = 2;

        // server
        while (i < a_text.size() && a_text[i] != L'\\')
        {
            ++i;
        }
        if (i >= a_text.size())
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "UNC path must contain a server name and a share name.");
        }
        ++i;

        // share
        while (i < a_text.size() && a_text[i] != L'\\')
        {
            ++i;
        }

        // share 末尾の "\" まで含める（あれば）
        if (i < a_text.size() && a_text[i] == L'\\')
        {
            ++i;
        }

        *a_outRootLength = i;
        return Result::ok();
    }
}

namespace Cue::PAL::Win
{
    Result WinFileSystem::executable_directory(Core::IO::Path& a_outDirectory) noexcept
    {
        a_outDirectory = {};

        wchar_t buffer[MAX_PATH]{};
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(MAX_PATH));
        if (written == 0 || written >= MAX_PATH)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Executable path could not be resolved.");
        }

        std::string executablePath{};
        Result result =
            wide_to_utf8(std::wstring_view(buffer, written), &executablePath);
        if (!result)
        {
            return result;
        }

        a_outDirectory = Core::IO::Path(executablePath).parent();
        return Result::ok();
    }

    Result WinFileSystem::exists(const Core::IO::Path& a_path, bool* a_outExists) noexcept
    {
        // 引数チェック
        if (a_outExists == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outExists = false; // デフォルトは存在しないとする
        
        // 変換
        std::wstring wpath;
        Result r = path_to_native_w(a_path, &wpath);
        if (!r)
        {
            return r; // 変換に失敗したらエラーを返す
        }

        // ファイル属性を取得して存在を確認する
        const DWORD attrs = ::GetFileAttributesW(wpath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            {
                *a_outExists = false;
                return Result::ok();
            }
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(e)), Severity::Error,
                "Failed to get file attributes.");
        }

        *a_outExists = true;

        return Result::ok();
    }

    Result WinFileSystem::stat(const Core::IO::Path& a_path, Core::IO::FileStat* a_outStat) noexcept
    {
        // 引数チェック
        if (a_outStat == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outStat = {};

        // 変換
        std::wstring wpath{};
        Result r = path_to_native_w(a_path, &wpath);
        if (!r)
        {
            return r;
        }

        // 属性取得
        WIN32_FILE_ATTRIBUTE_DATA data{};
        r = get_attrs(wpath, &data);
        if (!r)
        {
            return r;
        }

        // 種別
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
        {
            a_outStat->type = Cue::Core::IO::FileType::directory;
            a_outStat->size_bytes = 0;
        }
        else
        {
            a_outStat->type = Cue::Core::IO::FileType::regular;
            ULARGE_INTEGER sz{};
            sz.LowPart = data.nFileSizeLow;
            sz.HighPart = data.nFileSizeHigh;
            a_outStat->size_bytes = sz.QuadPart;
        }

        // mtime
        a_outStat->mtime_ns = filetime_to_unix_ns(data.ftLastWriteTime);

        return Result::ok();
    }

    Result WinFileSystem::create_directories(const Core::IO::Path& a_path) noexcept
    {
        // 変換
        std::wstring wpath{};
        Result r = path_to_native_w(a_path, &wpath);
        if (!r)
        {
            return r;
        }

        // 空は何もしない
        if (wpath.empty())
        {
            return Result::ok();
        }

        // ルートを決める（C:\ / \ / \\server\share\）
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

        // パスを走査して段階的に作成
        std::wstring cur = wpath.substr(0, i);

        while (i < wpath.size())
        {
            // 区切りをスキップ
            while (i < wpath.size() && wpath[i] == L'\\')
            {
                ++i;
            }
            if (i >= wpath.size())
            {
                break;
            }

            // 次のセグメント
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

            // cur に追加
            if (!cur.empty() && cur.back() != L'\\')
            {
                cur.push_back(L'\\');
            }
            cur.append(seg.data(), seg.size());

            // 作成
            r = create_dir_if_needed(cur);
            if (!r)
            {
                return r;
            }
        }

        return Result::ok();
    }

    Result WinFileSystem::list_directory(const Core::IO::Path& a_path, std::vector<Core::IO::Path>* a_outEntries) noexcept
    {
        // 引数チェック
        if (a_outEntries == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        a_outEntries->clear();

        // native path
        std::wstring wdir{};
        Result r = path_to_native_w(a_path, &wdir);
        if (!r)
        {
            return r;
        }

        // パターン作成
        std::wstring pattern{};
        r = build_find_pattern(wdir, &pattern);
        if (!r)
        {
            return r;
        }

        // FindFirst/Next
        WIN32_FIND_DATAW fd{};
        HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to list directory.");
        }

        // ループ
        for (;;)
        {
            // "." ".." を除外
            if (fd.cFileName[0] != L'\0')
            {
                const bool isDot = (wcscmp(fd.cFileName, L".") == 0);
                const bool isDotDot = (wcscmp(fd.cFileName, L"..") == 0);
                if (!isDot && !isDotDot)
                {
                    // full path を作る
                    std::wstring wfull = wdir;
                    if (!wfull.empty() && wfull.back() != L'\\')
                    {
                        wfull.push_back(L'\\');
                    }
                    wfull += fd.cFileName;

                    // wide -> utf8
                    std::string utf8{};
                    r = wide_to_utf8(wfull, &utf8);
                    if (!r)
                    {
                        (void)::FindClose(hFind);
                        return r;
                    }

                    // "\" -> "/" に戻す（Pathの内部規約）
                    for (char& c : utf8)
                    {
                        if (c == '\\')
                        {
                            c = '/';
                        }
                    }

                    a_outEntries->push_back(Cue::Core::IO::Path{ std::move(utf8) }.normalize());
                }
            }

            // 次
            if (::FindNextFileW(hFind, &fd) == FALSE)
            {
                const DWORD e = ::GetLastError();
                (void)::FindClose(hFind);

                if (e == ERROR_NO_MORE_FILES)
                {
                    return Result::ok();
                }
                return Result::fail(
                    convert_hresult_code(HRESULT_FROM_WIN32(e)), Severity::Error,
                "Failed to list directory.");
            }
        }
    }

    Result WinFileSystem::remove(const Core::IO::Path& a_path, bool* a_outRemoved) noexcept
    {
        // 引数チェック
        if (a_outRemoved == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outRemoved = false;

        // native
        std::wstring wpath{};
        Result r = path_to_native_w(a_path, &wpath);
        if (!r)
        {
            return r;
        }

        // 属性確認
        const DWORD attrs = ::GetFileAttributesW(wpath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            {
                *a_outRemoved = false;
                return Result::ok();
            }
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(e)), Severity::Error,
                "Failed to get file attributes.");
        }

        // ディレクトリ or ファイル
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u)
        {
            if (::RemoveDirectoryW(wpath.c_str()) == FALSE)
            {
                return Result::fail(
                    convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                    "Failed to remove directory.");
            }
        }
        else
        {
            if (::DeleteFileW(wpath.c_str()) == FALSE)
            {
                return Result::fail(
                    convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to remove file.");
            }
        }

        *a_outRemoved = true;
        return Result::ok();
    }

    Result WinFileSystem::rename(const Core::IO::Path& a_from, const Core::IO::Path& a_to) noexcept
    {
        // 変換
        std::wstring wfrom{};
        std::wstring wto{};

        Result r = path_to_native_w(a_from, &wfrom);
        if (!r)
        {
            return r;
        }

        r = path_to_native_w(a_to, &wto);
        if (!r)
        {
            return r;
        }

        // 置換ありで MoveFileExW（write-temp -> atomic replace に使う）
        const DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED;
        if (::MoveFileExW(wfrom.c_str(), wto.c_str(), flags) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to rename file or directory.");
        }

        return Result::ok();
    }

    Result WinFileSystem::copy_file(
        const Core::IO::Path& a_from,
        const Core::IO::Path& a_to,
        bool a_overwrite) noexcept
    {
        std::wstring wfrom{};
        std::wstring wto{};

        Result result = path_to_native_w(a_from, &wfrom);
        if (!result)
        {
            return result;
        }

        result = path_to_native_w(a_to, &wto);
        if (!result)
        {
            return result;
        }

        if (::CopyFileW(
                wfrom.c_str(),
                wto.c_str(),
                a_overwrite ? FALSE : TRUE) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())),
                Severity::Error,
                "Failed to copy file.");
        }

        return Result::ok();
    }

    Result WinFileSystem::path_to_native_w(const Core::IO::Path& a_path, std::wstring* a_outText) noexcept
    {
        // UTF-8 -> UTF-16
        Result r = utf8_to_wide(a_path.utf8(), a_outText);
        if (!r)
        {
            return r;
        }

        // 区切りを Win に合わせる
        to_native_seps(a_outText);

        return Result::ok();
    }

    Result WinFileSystem::open(const Core::IO::Path& a_path, const Core::IO::FileOpenDesc& a_desc, std::unique_ptr<Core::IO::IFile>* a_outFile) noexcept
    {
        // 引数チェック
        if (a_outFile == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        a_outFile->reset();

        // no_buffer 指定は unsupported を返す（整列制約は未対応）
        if (Cue::Core::IO::has_flag(a_desc.flags, Cue::Core::IO::OpenFlags::no_buffer))
        {
            return Result::fail(
                Code::Unsupported, Severity::Error,
                "Unbuffered I/O is not supported on this platform.");
        }

        // 変換
        std::wstring wpath{};
        Result r = path_to_native_w(a_path, &wpath);
        if (!r)
        {
            return r;
        }

        // DesiredAccess
        DWORD desired = 0;
        switch (a_desc.access)
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
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file access mode.");
        }

        // append
        if (Cue::Core::IO::has_flag(a_desc.flags, Cue::Core::IO::OpenFlags::append))
        {
            // 書き込み時は FILE_APPEND_DATA を足す（GENERIC_WRITE でも動くが意図が明確）
            desired |= FILE_APPEND_DATA;
        }

        // CreationDisposition
        DWORD disposition = OPEN_EXISTING;
        switch (a_desc.create)
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
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file creation mode.");
        }

        // FlagsAndAttributes（ヒント）
        DWORD flags = FILE_ATTRIBUTE_NORMAL;
        if (Cue::Core::IO::has_flag(a_desc.flags, Cue::Core::IO::OpenFlags::sequential))
        {
            flags |= FILE_FLAG_SEQUENTIAL_SCAN;
        }
        if (Cue::Core::IO::has_flag(a_desc.flags, Cue::Core::IO::OpenFlags::random))
        {
            flags |= FILE_FLAG_RANDOM_ACCESS;
        }

        // Share（Editor運用で重要）
        const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

        // CreateFileW
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
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to open file.");
        }

        // append の場合、末尾へ
        if (Cue::Core::IO::has_flag(a_desc.flags, Cue::Core::IO::OpenFlags::append))
        {
            LARGE_INTEGER zero{};
            zero.QuadPart = 0;

            LARGE_INTEGER newPos{};
            if (::SetFilePointerEx(h, zero, &newPos, FILE_END) == FALSE)
            {
                const DWORD e = ::GetLastError();
                (void)::CloseHandle(h);
                return Result::fail(
                    convert_hresult_code(HRESULT_FROM_WIN32(e)), Severity::Error,
                    "Failed to seek to end of file for append.");
            }
        }

        // wrap
        *a_outFile = std::unique_ptr<Cue::Core::IO::IFile>(new WinFile(h));
        return Result::ok();
    }

    Result WinFileSystem::read_all(const Core::IO::Path& a_path, std::vector<std::byte>* a_outData) noexcept
    {
        // 引数チェック
        if (a_outData == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        a_outData->clear();

        // open
        Cue::Core::IO::FileOpenDesc d{};
        d.access = Cue::Core::IO::OpenAccess::read;
        d.create = Cue::Core::IO::OpenCreate::open_existing;
        d.flags = Cue::Core::IO::OpenFlags::sequential;

        std::unique_ptr<Cue::Core::IO::IFile> f{};
        Result r = open(a_path, d, &f);
        if (!r)
        {
            return r;
        }

        // size
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
            return Result::fail(
                Code::OutOfMemory, Severity::Error,
                "File is too large to read into memory.");
        }

        // 読み込み
        a_outData->resize(static_cast<size_t>(sz));

        uint64_t got = 0;
        r = f->read(std::span<std::byte>{ a_outData->data(), a_outData->size() }, &got);
        if (!r)
        {
            (void)f->close();
            a_outData->clear();
            return r;
        }

        // 期待サイズ未満なら詰める
        if (got < static_cast<uint64_t>(a_outData->size()))
        {
            a_outData->resize(static_cast<size_t>(got));
        }

        (void)f->close();
        return Result::ok();
    }

    Result WinFileSystem::write_all(const Core::IO::Path& a_path, std::span<const std::byte> a_data, bool a_createParentDirs) noexcept
    {
        // 親ディレクトリ作成
        if (a_createParentDirs)
        {
            const Cue::Core::IO::Path parent = a_path.parent();
            if (!parent.is_empty())
            {
                const Result r0 = create_directories(parent);
                if (!r0)
                {
                    return r0;
                }
            }
        }

        // open (create_always)
        Cue::Core::IO::FileOpenDesc d{};
        d.access = Cue::Core::IO::OpenAccess::write;
        d.create = Cue::Core::IO::OpenCreate::create_always;
        d.flags = Cue::Core::IO::OpenFlags::sequential;

        std::unique_ptr<Cue::Core::IO::IFile> f{};
        Result r = open(a_path, d, &f);
        if (!r)
        {
            return r;
        }

        // write
        uint64_t written = 0;
        r = f->write(a_data, &written);
        if (!r)
        {
            (void)f->close();
            return r;
        }

        if (written != static_cast<uint64_t>(a_data.size()))
        {
            (void)f->close();
            return Result::fail(
                Code::InternalError, Severity::Error,
                "Failed to write all data to file.");
        }

        // flush
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
