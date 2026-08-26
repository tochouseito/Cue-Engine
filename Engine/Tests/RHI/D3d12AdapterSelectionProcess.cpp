#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12AdapterSelectionProbe.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::int64_t k_noHardwareAdapter = 24;
constexpr std::int64_t k_noSuitableAdapter = 25;
constexpr std::int64_t k_adapterLogFailed = 27;

class ProcessFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

class ProcessLogSink final : public cue::LogSink
{
  public:
    /// @brief ProcessLogSink を必要な依存と初期状態から構築する
    explicit ProcessLogSink(bool &a_sawFactoryFallbackWarning) noexcept
        : m_sawFactoryFallbackWarning(a_sawFactoryFallbackWarning)
    {
    }

    /// @brief 受け取った Log Record を対象 Sink へ書き込み、出力成否を返す
    [[nodiscard]] bool write(const cue::LogRecord &a_record) override
    {
        if (a_record.level() == cue::LogLevel::Warning &&
            a_record.message() == "DXGI Debug Factoryを利用できないため診断なしで続行します")
        {
            m_sawFactoryFallbackWarning = true;
        }

        return true;
    }

    /// @brief 対象 Sink に保留中の Log 出力を反映し、完了成否を返す
    [[nodiscard]] bool flush() override
    {
        return true;
    }

  private:
    bool &m_sawFactoryFallbackWarning;
};

class FailingLogSink final : public cue::LogSink
{
  public:
    /// @brief 受け取った Log Record を対象 Sink へ書き込み、出力成否を返す
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        return false;
    }

    /// @brief 対象 Sink に保留中の Log 出力を反映し、完了成否を返す
    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

/// @brief D3d12AdapterSelectionProcess Test の Selection が期待する契約を満たすか検証する
[[nodiscard]] int validate_selection(
    const cue::D3d12AdapterSelectionProbeReport &a_report, cue::GraphicsAdapterKind a_expectedKind) noexcept
{
    if (a_report.adapterName.empty())
    {
        return 10;
    }

    if (a_report.adapterKind != a_expectedKind)
    {
        return 11;
    }

    return a_report.isFeatureLevel12_0 ? 0 : 12;
}


/// @brief D3d12AdapterSelectionProcess Test の Independent Supported Hardware Adapter 条件を判定して返す
[[nodiscard]] bool has_independent_supported_hardware_adapter() noexcept
{
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;

    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
    {
        return true;
    }

    for (UINT index = 0;; ++index)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        HRESULT enumerationResult = factory->EnumAdapters1(index, &adapter);

        if (enumerationResult == DXGI_ERROR_NOT_FOUND)
        {
            return false;
        }

        if (FAILED(enumerationResult))
        {
            return true;
        }

        DXGI_ADAPTER_DESC1 description = {};

        if (FAILED(adapter->GetDesc1(&description)))
        {
            return true;
        }

        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(D3D12CreateDevice(
                adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            return true;
        }
    }
}
} // namespace

/// @brief 指定 Scenario で Adapter 選択を実行し、選択結果と失敗診断を Process 単位で検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    std::string_view mode = a_arguments[1];

    if (mode != "Hardware" && mode != "Warp" && mode != "Skip" && mode != "Mapping" &&
        mode != "FactoryFallback" && mode != "FactoryFallbackLogFailure" && mode != "LogFailure")
    {
        return 2;
    }

    ProcessFatalHandler fatalHandler;
    bool sawFactoryFallbackWarning = false;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    bool useFailingSink = mode == "LogFailure" || mode == "FactoryFallbackLogFailure";
    sinks.push_back(useFailingSink
                        ? std::unique_ptr<cue::LogSink>(std::make_unique<FailingLogSink>())
                        : std::unique_ptr<cue::LogSink>(
                              std::make_unique<ProcessLogSink>(sawFactoryFallbackWarning)));
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);

    if (mode == "Skip")
    {
        return cue::verify_d3d12_unsupported_candidate_skip_for_probe() ? 0 : 3;
    }

    if (mode == "Mapping")
    {
        return cue::verify_d3d12_capability_mapping_for_probe(assertContext) ? 0 : 4;
    }

    if (mode == "FactoryFallback")
    {
        cue::Result<cue::D3d12FactoryFallbackProbeReport> fallbackResult =
            cue::probe_d3d12_factory_fallback(assertContext);
        return fallbackResult && fallbackResult.try_value()->requestedDebugFactory &&
                       fallbackResult.try_value()->retriedWithoutDebug && sawFactoryFallbackWarning
                   ? 0
                   : 6;
    }

    if (mode == "FactoryFallbackLogFailure")
    {
        cue::Result<cue::D3d12FactoryFallbackProbeReport> fallbackResult =
            cue::probe_d3d12_factory_fallback(assertContext);
        const cue::Error *error = fallbackResult.try_error();
        return !fallbackResult && error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
                       error->code().value() == k_adapterLogFailed
                   ? 0
                   : 9;
    }

    bool useWarp = mode == "Warp" || mode == "LogFailure";
    cue::D3d12AdapterPolicy policy = useWarp ? cue::D3d12AdapterPolicy::Warp
                                             : cue::D3d12AdapterPolicy::HighPerformanceHardware;
    cue::Result<cue::D3d12AdapterSelectionProbeReport> result =
        cue::probe_d3d12_adapter_selection(policy, assertContext);

    if (!result)
    {
        const cue::Error *error = result.try_error();
        if (mode == "LogFailure")
        {
            return error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
                           error->code().value() == k_adapterLogFailed
                       ? 0
                       : 7;
        }

        bool isHardwareUnavailable = mode == "Hardware" && !has_independent_supported_hardware_adapter() &&
                                     error != nullptr &&
                                     error->code().domain() == "Cue.RHI.D3D12" &&
                                     (error->code().value() == k_noHardwareAdapter ||
                                      error->code().value() == k_noSuitableAdapter);
        return isHardwareUnavailable ? 77 : 5;
    }

    if (mode == "LogFailure")
    {
        return 8;
    }

    cue::GraphicsAdapterKind expectedKind = mode == "Warp" ? cue::GraphicsAdapterKind::Software
                                                            : cue::GraphicsAdapterKind::Hardware;
    return validate_selection(*result.try_value(), expectedKind);
}
