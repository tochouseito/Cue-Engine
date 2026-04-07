#pragma once

// === C++ includes ===
#include <typeinfo>

namespace Cue::ECS
{
    extern "C" __declspec(dllimport)
    size_t ECS_RegisterComponentID(const char* uniqueName);

    template<class T>
    inline size_t ComponentID()
    {
        // ※ RTTI がオフなら #T の文字列などにする
        static size_t id = ECS_RegisterComponentID(typeid(T).name());
        return id;
    }
}
