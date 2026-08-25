// DXGI Adapter 候補を列挙し、要求 Feature Level と選択 Policy に合う候補だけを D3D12 Backend へ渡す
// 候補を除外した理由も Log へ残し、GPU 構成差による初期化失敗を追跡できるようにする

#include "D3d12AdapterSelection.h"

#include "D3d12Diagnostics.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Log.h>

#include <Windows.h>

#include <cstdlib>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
constexpr std::int64_t k_factoryCreationFailed = 20;
constexpr std::int64_t k_adapterEnumerationFailed = 21;
constexpr std::int64_t k_adapterDescriptionFailed = 22;
constexpr std::int64_t k_adapterNameFailed = 23;
constexpr std::int64_t k_noHardwareAdapter = 24;
constexpr std::int64_t k_noSuitableAdapter = 25;
constexpr std::int64_t k_warpUnavailable = 26;
constexpr std::int64_t k_adapterLogFailed = 27;
constexpr D3D_FEATURE_LEVEL k_requiredFeatureLevel = D3D_FEATURE_LEVEL_12_0;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("D3D12 adapter selection allocation failed");
    std::abort();
}

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary);
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, HRESULT a_nativeCode,
                                           std::string_view a_nativeDomain = "DXGI") noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    cue::NativeError nativeError = cue::NativeError::create(
        a_context.fatal_handler(), a_nativeDomain, static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}

[[nodiscard]] cue::Error make_win32_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                          std::string_view a_summary, DWORD a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    cue::NativeError nativeError = cue::NativeError::create(
        a_context.fatal_handler(), "Win32", static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}

[[nodiscard]] cue::Result<void> validate_log_result(cue::LogResult a_result,
                                                    const cue::AssertContext &a_context) noexcept
{
    if (a_result == cue::LogResult::Success)
    {
        return cue::Result<void>::success();
    }

    return cue::Result<void>::failure(
        make_error(a_context, k_adapterLogFailed, "Foundation Logger could not record DXGI adapter diagnostics"));
}

[[nodiscard]] cue::Result<std::string> convert_adapter_name(
    std::wstring_view a_name, const cue::AssertContext &a_context) noexcept
{
    if (a_name.empty() || a_name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return cue::Result<std::string>::failure(
            make_error(a_context, k_adapterNameFailed, "DXGI adapter name is invalid"));
    }

    int sourceLength = static_cast<int>(a_name.size());
    int convertedLength = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, a_name.data(), sourceLength, nullptr, 0, nullptr, nullptr);

    if (convertedLength == 0)
    {
        DWORD nativeCode = GetLastError();
        return cue::Result<std::string>::failure(make_win32_error(
            a_context, k_adapterNameFailed, "DXGI adapter name is not valid UTF-16", nativeCode));
    }

    try
    {
        std::string result(static_cast<std::size_t>(convertedLength), '\0');
        int writtenLength = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, a_name.data(), sourceLength, result.data(), convertedLength,
            nullptr, nullptr);

        if (writtenLength != convertedLength)
        {
            DWORD nativeCode = GetLastError();
            return cue::Result<std::string>::failure(make_win32_error(
                a_context, k_adapterNameFailed, "DXGI adapter name conversion failed", nativeCode));
        }

        return cue::Result<std::string>::success(std::move(result));
    }
    catch (...)
    {
        terminate_allocation(a_context);
    }
}

[[nodiscard]] cue::Result<std::wstring_view> get_adapter_name(
    const DXGI_ADAPTER_DESC3 &a_description, const cue::AssertContext &a_context) noexcept
{
    std::size_t length = 0;

    while (length < std::size(a_description.Description) && a_description.Description[length] != L'\0')
    {
        ++length;
    }

    if (length == std::size(a_description.Description))
    {
        return cue::Result<std::wstring_view>::failure(
            make_error(a_context, k_adapterNameFailed, "DXGI adapter name is not terminated"));
    }

    return cue::Result<std::wstring_view>::success(
        std::wstring_view(a_description.Description, length));
}

[[nodiscard]] cue::Result<void> log_skipped_candidate(
    UINT a_index, HRESULT a_probeResult, const cue::AssertContext &a_context) noexcept
{
    cue::Error error = make_native_error(
        a_context, k_noSuitableAdapter, "DXGI adapter does not satisfy D3D Feature Level 12_0",
        a_probeResult, "D3D12");

    try
    {
        error.add_context(a_context.fatal_handler(), "AdapterIndex=" + std::to_string(a_index));
    }
    catch (...)
    {
        terminate_allocation(a_context);
    }

    cue::LogResult logResult = a_context.logger().log(
        cue::LogLevel::Info, "D3D12未対応AdapterをSkipします", std::move(error));
    return validate_log_result(logResult, a_context);
}

[[nodiscard]] cue::Result<void> log_software_candidate(
    UINT a_index, const cue::AssertContext &a_context) noexcept
{
    cue::Error error = make_error(
        a_context, k_noSuitableAdapter, "Software DXGI adapter is excluded by hardware policy");

    try
    {
        error.add_context(a_context.fatal_handler(), "AdapterIndex=" + std::to_string(a_index));
    }
    catch (...)
    {
        terminate_allocation(a_context);
    }

    cue::LogResult logResult = a_context.logger().log(
        cue::LogLevel::Debug, "Software AdapterをHardware候補から除外します", std::move(error));
    return validate_log_result(logResult, a_context);
}

[[nodiscard]] cue::Result<cue::D3d12AdapterReport> make_report(
    const DXGI_ADAPTER_DESC3 &a_description, cue::GraphicsAdapterKind a_kind,
    const cue::AssertContext &a_context) noexcept
{
    cue::Result<std::wstring_view> nameViewResult = get_adapter_name(a_description, a_context);

    if (!nameViewResult)
    {
        return cue::Result<cue::D3d12AdapterReport>::failure(std::move(*nameViewResult.try_error()));
    }

    cue::Result<std::string> nameResult = convert_adapter_name(*nameViewResult.try_value(), a_context);

    if (!nameResult)
    {
        return cue::Result<cue::D3d12AdapterReport>::failure(std::move(*nameResult.try_error()));
    }

    cue::D3d12AdapterReport report = {};
    report.adapterName = std::move(*nameResult.try_value());
    report.dedicatedVideoMemoryBytes = static_cast<std::uint64_t>(a_description.DedicatedVideoMemory);
    report.vendorId = a_description.VendorId;
    report.deviceId = a_description.DeviceId;
    report.adapterKind = a_kind;
    return cue::Result<cue::D3d12AdapterReport>::success(std::move(report));
}

[[nodiscard]] cue::Result<void> log_factory_debug_fallback(
    HRESULT a_nativeCode, const cue::AssertContext &a_context) noexcept
{
    cue::Error error = make_native_error(
        a_context, k_factoryCreationFailed,
        "DXGI debug component is unavailable; factory creation will continue without debug",
        a_nativeCode);
    cue::LogResult logResult = a_context.logger().log(
        cue::LogLevel::Warning, "DXGI Debug Factoryを利用できないため診断なしで続行します", std::move(error));
    return validate_log_result(logResult, a_context);
}

[[nodiscard]] cue::Result<void> log_selection(
    const cue::D3d12AdapterReport &a_report, const cue::AssertContext &a_context) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(
        a_context.fatal_handler(), "DXGI.Adapter.DeviceId", static_cast<std::int64_t>(a_report.deviceId));
    cue::NativeError vendorId = cue::NativeError::create(
        a_context.fatal_handler(), "DXGI.Adapter.VendorId", static_cast<std::int64_t>(a_report.vendorId));
    cue::Error error = cue::Error::create(
        a_context.fatal_handler(), std::move(code), a_report.adapterName, std::move(vendorId));

    try
    {
        error.add_context(
            a_context.fatal_handler(),
            "DedicatedVideoMemoryBytes=" + std::to_string(a_report.dedicatedVideoMemoryBytes));
        error.add_context(a_context.fatal_handler(), "D3D_FEATURE_LEVEL_12_0");
    }
    catch (...)
    {
        terminate_allocation(a_context);
    }

    cue::LogResult logResult = a_context.logger().log(
        cue::LogLevel::Info, "D3D12 Adapterを選択しました", std::move(error));
    return validate_log_result(logResult, a_context);
}

[[nodiscard]] cue::Result<cue::D3d12AdapterSelection> make_selection(
    Microsoft::WRL::ComPtr<IDXGIFactory6> a_factory,
    Microsoft::WRL::ComPtr<IDXGIAdapter4> a_adapter, const DXGI_ADAPTER_DESC3 &a_description,
    cue::GraphicsAdapterKind a_kind, const cue::AssertContext &a_context) noexcept
{
    cue::Result<cue::D3d12AdapterReport> reportResult = make_report(a_description, a_kind, a_context);

    if (!reportResult)
    {
        return cue::Result<cue::D3d12AdapterSelection>::failure(std::move(*reportResult.try_error()));
    }

    cue::Result<void> logResult = log_selection(*reportResult.try_value(), a_context);

    if (!logResult)
    {
        return cue::Result<cue::D3d12AdapterSelection>::failure(std::move(*logResult.try_error()));
    }

    cue::D3d12AdapterSelection selection = {};
    selection.factory = std::move(a_factory);
    selection.adapter = std::move(a_adapter);
    selection.report = std::move(*reportResult.try_value());
    selection.featureLevel = k_requiredFeatureLevel;
    return cue::Result<cue::D3d12AdapterSelection>::success(std::move(selection));
}

HRESULT WINAPI create_dxgi_factory(UINT a_flags, REFIID a_interfaceId, void **a_factory) noexcept
{
    return CreateDXGIFactory2(a_flags, a_interfaceId, a_factory);
}
} // namespace

namespace cue
{
bool is_supported_hardware_candidate(D3d12AdapterCandidateFacts a_candidate) noexcept
{
    return !a_candidate.isSoftware && a_candidate.supportsRequiredFeatureLevel;
}

bool should_retry_without_debug_factory(HRESULT a_result, bool a_wasDebugRequested) noexcept
{
    return a_wasDebugRequested && a_result == DXGI_ERROR_SDK_COMPONENT_MISSING;
}

Result<Microsoft::WRL::ComPtr<IDXGIFactory6>> create_d3d12_factory(
    UINT a_flags, D3d12FactoryCreator a_creator, const AssertContext &a_assertContext) noexcept
{
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    HRESULT factoryResult = a_creator(a_flags, IID_PPV_ARGS(&factory));

    if (should_retry_without_debug_factory(factoryResult, a_flags != 0))
    {
        Result<void> fallbackResult = log_factory_debug_fallback(factoryResult, a_assertContext);

        if (!fallbackResult)
        {
            return Result<Microsoft::WRL::ComPtr<IDXGIFactory6>>::failure(
                std::move(*fallbackResult.try_error()));
        }

        factory.Reset();
        factoryResult = a_creator(0, IID_PPV_ARGS(&factory));
    }

    if (FAILED(factoryResult))
    {
        return Result<Microsoft::WRL::ComPtr<IDXGIFactory6>>::failure(make_native_error(
            a_assertContext, k_factoryCreationFailed, "DXGI Factory could not be created", factoryResult));
    }

    return Result<Microsoft::WRL::ComPtr<IDXGIFactory6>>::success(std::move(factory));
}

Result<CapabilityReport> make_d3d12_capability_report(
    const D3d12AdapterReport &a_report, bool a_isUma, const AssertContext &a_assertContext) noexcept
{
    try
    {
        CapabilityReport report = {};
        report.adapterName = a_report.adapterName;
        report.dedicatedVideoMemoryBytes = a_report.dedicatedVideoMemoryBytes;
        report.vendorId = a_report.vendorId;
        report.deviceId = a_report.deviceId;
        report.backendKind = GraphicsBackendKind::D3d12;
        report.adapterKind = a_report.adapterKind;
        report.profile = GraphicsProfile::Baseline3D;
        report.isUma = a_isUma;
        return Result<CapabilityReport>::success(std::move(report));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

// High Performance 順の列挙結果を使用し、明示的な独自順位付けを持たず OS の GPU 選択意図を尊重する
// Debug Factory が環境上利用できない場合だけ通常 Factory へ再試行し、他の生成失敗は隠さず返す
Result<D3d12AdapterSelection> select_d3d12_adapter(
    D3d12AdapterPolicy a_policy, const D3d12DiagnosticsStatus &a_diagnostics,
    const AssertContext &a_assertContext) noexcept
{
    UINT factoryFlags = a_diagnostics.isDebugLayerEnabled ? DXGI_CREATE_FACTORY_DEBUG : 0;
    Result<Microsoft::WRL::ComPtr<IDXGIFactory6>> factoryResult =
        create_d3d12_factory(factoryFlags, create_dxgi_factory, a_assertContext);

    if (!factoryResult)
    {
        return Result<D3d12AdapterSelection>::failure(std::move(*factoryResult.try_error()));
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory = std::move(*factoryResult.try_value());

    if (a_policy == D3d12AdapterPolicy::Warp)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
        HRESULT warpResult = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));

        if (FAILED(warpResult))
        {
            return Result<D3d12AdapterSelection>::failure(make_native_error(
                a_assertContext, k_warpUnavailable, "DXGI WARP adapter is unavailable", warpResult));
        }

        DXGI_ADAPTER_DESC3 description = {};
        HRESULT descriptionResult = adapter->GetDesc3(&description);

        if (FAILED(descriptionResult))
        {
            return Result<D3d12AdapterSelection>::failure(make_native_error(
                a_assertContext, k_adapterDescriptionFailed, "DXGI WARP adapter description is unavailable",
                descriptionResult));
        }

        HRESULT probeResult = D3D12CreateDevice(
            adapter.Get(), k_requiredFeatureLevel, __uuidof(ID3D12Device), nullptr);

        if (FAILED(probeResult))
        {
            return Result<D3d12AdapterSelection>::failure(make_native_error(
                a_assertContext, k_noSuitableAdapter, "DXGI WARP adapter does not support D3D Feature Level 12_0",
                probeResult, "D3D12"));
        }

        return make_selection(
            std::move(factory), std::move(adapter), description, GraphicsAdapterKind::Software, a_assertContext);
    }

    bool sawHardwareAdapter = false;

    for (UINT index = 0;; ++index)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
        HRESULT enumerationResult = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));

        if (enumerationResult == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        if (FAILED(enumerationResult))
        {
            return Result<D3d12AdapterSelection>::failure(make_native_error(
                a_assertContext, k_adapterEnumerationFailed, "DXGI adapter enumeration failed",
                enumerationResult));
        }

        DXGI_ADAPTER_DESC3 description = {};
        HRESULT descriptionResult = adapter->GetDesc3(&description);

        if (FAILED(descriptionResult))
        {
            return Result<D3d12AdapterSelection>::failure(make_native_error(
                a_assertContext, k_adapterDescriptionFailed, "DXGI adapter description is unavailable",
                descriptionResult));
        }

        bool isSoftware = (description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0;

        if (isSoftware)
        {
            Result<void> skipLogResult = log_software_candidate(index, a_assertContext);

            if (!skipLogResult)
            {
                return Result<D3d12AdapterSelection>::failure(std::move(*skipLogResult.try_error()));
            }

            continue;
        }

        sawHardwareAdapter = true;
        HRESULT probeResult = D3D12CreateDevice(
            adapter.Get(), k_requiredFeatureLevel, __uuidof(ID3D12Device), nullptr);
        D3d12AdapterCandidateFacts facts = {false, SUCCEEDED(probeResult)};

        if (is_supported_hardware_candidate(facts))
        {
            return make_selection(
                std::move(factory), std::move(adapter), description, GraphicsAdapterKind::Hardware,
                a_assertContext);
        }

        Result<void> skipLogResult = log_skipped_candidate(index, probeResult, a_assertContext);

        if (!skipLogResult)
        {
            return Result<D3d12AdapterSelection>::failure(std::move(*skipLogResult.try_error()));
        }
    }

    std::int64_t errorCode = sawHardwareAdapter ? k_noSuitableAdapter : k_noHardwareAdapter;
    std::string_view summary = sawHardwareAdapter
                                   ? "No hardware DXGI adapter supports D3D Feature Level 12_0"
                                   : "No hardware DXGI adapter is available";
    return Result<D3d12AdapterSelection>::failure(make_error(a_assertContext, errorCode, summary));
}
} // namespace cue
