// DirectoryDialog の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue::PAL::Win
{
    Result pick_directory_dialog(
        std::string_view a_title,
        std::string_view a_initialDirectory,
        std::string* a_outSelectedDirectory,
        bool* a_outWasSelected
    ) noexcept;
}
