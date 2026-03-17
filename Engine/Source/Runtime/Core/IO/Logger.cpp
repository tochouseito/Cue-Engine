#include "core_pch.h"
#include "Logger.h"

#ifdef CUE_DEBUG
#include <Windows.h>
#endif

namespace Cue::Core::IO
{
    void out_debug_console(std::string_view message)
    {
#ifdef CUE_DEBUG
        ::OutputDebugStringA(message.data());
#endif // CUE_DEBUG
    }
}
