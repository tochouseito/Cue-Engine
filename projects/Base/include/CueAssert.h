#pragma once
#ifdef CUE_DEBUG
#include <cassert>
#include <format>
#include "Result.h"

namespace Cue
{
    class Assert final
    {
    public:
        template<typename... Args>
        static void cue_assert(bool expr, const char* message, Args&&... args)
        {
            std::string formatted_message = std::vformat(message, std::make_format_args(args...));

            assert(expr && formatted_message.c_str());
        }

        template<typename... Args>
        static void cue_assert(Result expr, const char* message, Args&&... args)
        {
            std::string formatted_message = std::vformat(message, std::make_format_args(args...));

            assert(expr && formatted_message.c_str());
        }
    };
}

#endif // CUE_DEBUG
