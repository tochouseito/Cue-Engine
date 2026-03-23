// === Core includes ===
#include "Core_pch.h"
#include "Logger.h"

// === Windows API includes ===
#ifdef CUE_DEBUG
#include <Windows.h>
#endif

namespace Cue::Core::IO
{
    void out_debug_console(std::string_view a_message)
    {
#ifdef CUE_DEBUG
        ::OutputDebugStringA(a_message.data());
#endif // CUE_DEBUG
    }
}
