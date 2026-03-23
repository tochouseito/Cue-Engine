#include "WinFile.h"

namespace Cue::PAL::Win
{
    WinFile::WinFile(HANDLE a_handle) noexcept
        : m_handle(a_handle)
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

    Result WinFile::read(std::span<std::byte> a_destination, uint64_t* a_outRead) noexcept
    {
        // 1) 引数チェック
        if (a_outRead == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outRead = 0;

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
        }

        // 2) 0バイト
        if (a_destination.size() == 0)
        {
            return Result::ok();
        }

        // 3) ReadFile (最大DWORDずつ)
        uint64_t total = 0;
        size_t offset = 0;

        while (offset < a_destination.size())
        {
            const size_t remain = a_destination.size() - offset;
            const DWORD chunk = (remain > static_cast<size_t>(MAXDWORD)) ? MAXDWORD : static_cast<DWORD>(remain);

            DWORD readBytes = 0;
            if (::ReadFile(m_handle, a_destination.data() + offset, chunk, &readBytes, nullptr) == FALSE)
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

        *a_outRead = total;
        return Result::ok();
    }

    Result WinFile::write(std::span<const std::byte> a_source, uint64_t* a_outWritten) noexcept
    {
        // 1) 引数チェック
        if (a_outWritten == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outWritten = 0;

        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid file handle.");
        }

        // 2) 0バイト
        if (a_source.size() == 0)
        {
            return Result::ok();
        }

        // 3) WriteFile (最大DWORDずつ)
        uint64_t total = 0;
        size_t offset = 0;

        while (offset < a_source.size())
        {
            const size_t remain = a_source.size() - offset;
            const DWORD chunk = (remain > static_cast<size_t>(MAXDWORD)) ? MAXDWORD : static_cast<DWORD>(remain);

            DWORD written = 0;
            if (::WriteFile(m_handle, a_source.data() + offset, chunk, &written, nullptr) == FALSE)
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

        *a_outWritten = total;
        return Result::ok();
    }

    Result WinFile::seek(int64_t a_offset, Cue::Core::IO::SeekOrigin a_origin) noexcept
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
        switch (a_origin)
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
        li.QuadPart = a_offset;

        LARGE_INTEGER newPos{};
        if (::SetFilePointerEx(m_handle, li, &newPos, moveMethod) == FALSE)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to seek file.");
        }

        return Result::ok();
    }

    Result WinFile::tell(uint64_t* a_outPosition) noexcept
    {
        // 1) 引数チェック
        if (a_outPosition == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outPosition = 0;

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

        *a_outPosition = static_cast<uint64_t>(newPos.QuadPart);
        return Result::ok();
    }

    Result WinFile::size(uint64_t* a_outSize) noexcept
    {
        // 1) 引数チェック
        if (a_outSize == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }
        *a_outSize = 0;

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

        *a_outSize = static_cast<uint64_t>(sz.QuadPart);
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
