#include "Logger.h"

// === Windows includes ===
#ifdef CUE_DEBUG
#include <Windows.h>
#endif

namespace Cue::Core::IO
{
    void out_debug_console([[maybe_unused]] std::string_view a_message)
    {
#ifdef CUE_DEBUG
        // Windows のデバッグ出力へ出力
        ::OutputDebugStringA(a_message.data());
#endif // CUE_DEBUG
    }
}
