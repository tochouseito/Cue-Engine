#include "D3d12DeviceCreation.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <utility>

namespace
{
constexpr std::int64_t k_deviceCreationFailed = 30;

HRESULT WINAPI create_native_device(IUnknown *a_adapter, D3D_FEATURE_LEVEL a_featureLevel,
                                    REFIID a_interfaceId, void **a_device) noexcept
{
    return D3D12CreateDevice(a_adapter, a_featureLevel, a_interfaceId, a_device);
}

[[nodiscard]] cue::Error make_device_creation_error(
    const cue::AssertContext &a_context, HRESULT a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(
        a_context.fatal_handler(), "Cue.RHI.D3D12", k_deviceCreationFailed);
    cue::NativeError nativeError = cue::NativeError::create(
        a_context.fatal_handler(), "D3D12", static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(
        a_context.fatal_handler(), std::move(code), "D3D12 Device could not be created",
        std::move(nativeError));
}
} // namespace

namespace cue
{
Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_d3d12_device(
    IDXGIAdapter4 *a_adapter, D3D_FEATURE_LEVEL a_featureLevel,
    const AssertContext &a_assertContext) noexcept
{
    return create_d3d12_device(a_adapter, a_featureLevel, create_native_device, a_assertContext);
}

Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_d3d12_device(
    IDXGIAdapter4 *a_adapter, D3D_FEATURE_LEVEL a_featureLevel, D3d12DeviceCreator a_creator,
    const AssertContext &a_assertContext) noexcept
{
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    HRESULT creationResult = a_creator(a_adapter, a_featureLevel, IID_PPV_ARGS(&device));

    if (FAILED(creationResult))
    {
        return Result<Microsoft::WRL::ComPtr<ID3D12Device>>::failure(
            make_device_creation_error(a_assertContext, creationResult));
    }

    return Result<Microsoft::WRL::ComPtr<ID3D12Device>>::success(std::move(device));
}
} // namespace cue
