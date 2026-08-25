#include <Cue/Platform/WindowSystem.h>

namespace cue
{
// Platform Module に Destructor の単一定義を置き、Platform 実装の所有権を共通契約から解放できるようにする
WindowSystem::~WindowSystem() noexcept = default;
} // namespace cue
