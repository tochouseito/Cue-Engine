
// === C++ includes ===
#include <unordered_map>
#include <string>
#include <mutex>

namespace Cue::ECS
{
    extern "C" __declspec(dllexport)
    size_t ECS_RegisterComponentID(const char* uniqueName)   // C で export すると楽
    {
        static std::unordered_map<std::string, size_t> map;
        static size_t nextID = 0;
        static std::mutex m;
        std::lock_guard lk(m);
        auto [it, inserted] = map.try_emplace(uniqueName, nextID);
        if (inserted) ++nextID;          // 新規登録なら採番
        return it->second;               // 既存なら同じ ID を返す
    }
}
