#include "WinFile.h"

namespace Cue::PAL::Win
{
    // 暗黙変換禁止
    WinFile::WinFile(HANDLE h) noexcept
        : m_handle(h)
    {}
    WinFile::~WinFile()
    {
        // 1) close を呼ばれていなくてもリークさせない
        if (m_handle != INVALID_HANDLE_VALUE)
        {
            (void)::CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
    }
    Result WinFile::read(std::span<std::byte> dst, uint64_t* out_read) noexcept
    {
        // 1) 引数チェック
        if (out_read == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *out_read = 0;

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
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
                return Result::fail(
                    convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                    "Failed to read from file.");
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
    Result WinFile::write(std::span<const std::byte> src, uint64_t* out_written) noexcept
    {
        // 1) 引数チェック
        if (out_written == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *out_written = 0;

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
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
                return Result::fail(
                    convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                    "Failed to write to file.");
            }

            total += static_cast<uint64_t>(written);
            offset += static_cast<size_t>(written);

            // 4) 異常（書けない）
            if (written == 0)
            {
                return Result::fail(
                    Code::UnknownError, Severity::Error,
                    "Failed to write to file (0 bytes written).");
            }
        }

        *out_written = total;
        return Result::ok();
    }
    Result WinFile::seek(int64_t offset, Cue::Core::IO::SeekOrigin origin) noexcept
    {
        // 1) ハンドルチェック
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
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
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid seek origin.");
        }

        // 3) SetFilePointerEx
        LARGE_INTEGER li{};
        li.QuadPart = offset;

        LARGE_INTEGER newPos{};
        if (::SetFilePointerEx(m_handle, li, &newPos, moveMethod) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to seek file.");
        }

        return Result::ok();
    }
    Result WinFile::tell(uint64_t* out_pos) noexcept
    {
        // 1) 引数チェック
        if (out_pos == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *out_pos = 0;

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
        }

        // 2) 現在位置 = seek(0, current)
        LARGE_INTEGER zero{};
        zero.QuadPart = 0;

        LARGE_INTEGER newPos{};
        if (::SetFilePointerEx(m_handle, zero, &newPos, FILE_CURRENT) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to get current file position.");
        }

        if (newPos.QuadPart < 0)
        {
            return Result::fail(
                Code::UnknownError, Severity::Error,
                "Failed to get current file position (negative position).");
        }

        *out_pos = static_cast<uint64_t>(newPos.QuadPart);
        return Result::ok();
    }
    Result WinFile::size(uint64_t* out_size) noexcept
    {
        // 1) 引数チェック
        if (out_size == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *out_size = 0;

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
        }

        // 2) GetFileSizeEx
        LARGE_INTEGER sz{};
        if (::GetFileSizeEx(m_handle, &sz) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to get file size.");
        }

        if (sz.QuadPart < 0)
        {
            return Result::fail(
                Code::UnknownError, Severity::Error,
                "Failed to get file size (negative size).");
        }

        *out_size = static_cast<uint64_t>(sz.QuadPart);
        return Result::ok();
    }
    Result WinFile::flush() noexcept
    {
        // 1) ハンドルチェック
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
        }

        // 2) FlushFileBuffers
        if (::FlushFileBuffers(m_handle) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to flush file buffers.");
        }

        return Result::ok();
    }
    Result WinFile::close() noexcept
    {
        // 1) 既に閉じている
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::ok();
        }

        // 2) CloseHandle
        if (::CloseHandle(m_handle) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to close file handle.");
        }

        m_handle = INVALID_HANDLE_VALUE;
        return Result::ok();
    }
}
