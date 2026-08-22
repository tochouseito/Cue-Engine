#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <memory>
#include <type_traits>

static_assert(std::is_convertible_v<std::unique_ptr<cue::D3d12Backend>, std::unique_ptr<cue::GraphicsBackend>>);
static_assert(std::is_nothrow_destructible_v<std::unique_ptr<cue::GraphicsBackend>>);
