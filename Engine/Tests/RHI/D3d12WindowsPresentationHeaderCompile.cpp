#include <Cue/RHI/D3D12/Windows/D3d12WindowsPresentation.h>

#include <memory>
#include <type_traits>
#include <utility>

using D3d12WindowsPresentationFactoryResult =
    decltype(cue::create_d3d12_windows_presentation(std::declval<cue::D3d12Backend &>(), std::declval<cue::Window &>(),
                                                    std::declval<const cue::PresentationDescriptor &>()));

static_assert(
    std::is_same_v<D3d12WindowsPresentationFactoryResult, cue::Result<std::unique_ptr<cue::PresentationContext>>>);
