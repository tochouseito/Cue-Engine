#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <memory>
#include <type_traits>
#include <utility>

static_assert(std::is_abstract_v<cue::D3d12Backend>);
static_assert(std::is_base_of_v<cue::GraphicsBackend, cue::D3d12Backend>);
static_assert(std::has_virtual_destructor_v<cue::D3d12Backend>);

using D3d12FactoryResult = decltype(cue::create_d3d12_backend(
    std::declval<const cue::D3d12BackendDescriptor &>(), std::declval<cue::AssertContext &>()));

static_assert(std::is_same_v<D3d12FactoryResult, cue::Result<std::unique_ptr<cue::D3d12Backend>>>);
