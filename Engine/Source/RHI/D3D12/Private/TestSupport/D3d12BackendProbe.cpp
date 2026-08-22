#include <Cue/RHI/D3D12/TestSupport/D3d12BackendProbe.h>

#include "D3d12DeviceCreation.h"

#include <Cue/Foundation/Error.h>

namespace
{
constexpr HRESULT k_expectedFailure = DXGI_ERROR_DEVICE_REMOVED;

HRESULT WINAPI fail_device_creation(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **) noexcept
{
    return k_expectedFailure;
}
} // namespace

namespace cue
{
bool verify_d3d12_device_creation_failure_for_probe(
    const AssertContext &a_assertContext) noexcept
{
    Result<Microsoft::WRL::ComPtr<ID3D12Device>> result = create_d3d12_device(
        nullptr, D3D_FEATURE_LEVEL_12_0, fail_device_creation, a_assertContext);
    const Error *error = result.try_error();
    const NativeError *nativeError = error != nullptr ? error->try_native_error() : nullptr;
    return !result && error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
           nativeError != nullptr && nativeError->domain() == "D3D12" &&
           nativeError->value() == static_cast<std::int64_t>(k_expectedFailure);
}
} // namespace cue
