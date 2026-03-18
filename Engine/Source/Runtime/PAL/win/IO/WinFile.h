#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <vector>
#include <span>

// === Windows API include ===
#include "stdafx.h"
#include "ConvertHresult.h"
#include "ConvertUTF.h"

namespace Cue::PAL::Win
{
    class WinFile final : public Cue::Core::IO::IFile
    {
    public:
        // 暗黙変換禁止
        explicit WinFile(HANDLE h) noexcept;

        ~WinFile() override;

        Result read(std::span<std::byte> dst, uint64_t* out_read) noexcept override;

        Result write(std::span<const std::byte> src, uint64_t* out_written) noexcept override;

        Result seek(int64_t offset, Cue::Core::IO::SeekOrigin origin) noexcept override;

        Result tell(uint64_t* out_pos) noexcept override;

        Result size(uint64_t* out_size) noexcept override;

        Result flush() noexcept override;

        Result close() noexcept override;

    private:
        HANDLE m_handle = INVALID_HANDLE_VALUE;
    };
}
