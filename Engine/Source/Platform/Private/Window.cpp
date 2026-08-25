#include <Cue/Platform/Window.h>

namespace cue
{
// Platform Module に Destructor の単一定義を置き、派生実装を基底 Pointer 経由で安全に破棄する
Window::~Window() noexcept = default;
} // namespace cue
