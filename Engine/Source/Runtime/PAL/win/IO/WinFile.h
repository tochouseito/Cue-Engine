#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <span>

// === Windows API includes ===
#include "ConvertHresult.h"
#include "ConvertUTF.h"
#include "stdafx.h"

namespace Cue::PAL::Win
{
    /// @brief Windows の `HANDLE` を使うファイル実装です。
    class WinFile final : public Cue::Core::IO::IFile
    {
    public:
        /// @brief ファイルハンドルからインスタンスを構築します。
        explicit WinFile(HANDLE a_handle) noexcept;

        ~WinFile() override;

        Result read(std::span<std::byte> a_destination, uint64_t* a_outRead) noexcept override;

        Result write(std::span<const std::byte> a_source, uint64_t* a_outWritten) noexcept override;

        Result seek(int64_t a_offset, Cue::Core::IO::SeekOrigin a_origin) noexcept override;

        Result tell(uint64_t* a_outPosition) noexcept override;

        Result size(uint64_t* a_outSize) noexcept override;

        Result flush() noexcept override;

        Result close() noexcept override;

    private:
        HANDLE m_handle = INVALID_HANDLE_VALUE;
    };
}
