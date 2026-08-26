#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12BackendProbe.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
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
    /// @brief 受け取った Log Record を対象 Sink へ書き込み、出力成否を返す
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        return true;
    }

    /// @brief 対象 Sink に保留中の Log 出力を反映し、完了成否を返す
    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

/// @brief D3d12BackendProcess Test の Backend LifecycleScenario を実行し、検証結果を返す
[[nodiscard]] int run_backend_lifecycle(
    cue::D3d12AdapterPolicy a_policy, cue::GraphicsAdapterKind a_expectedKind,
    cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12BackendDescriptor descriptor = {
        a_policy,
        cue::D3d12ValidationMode::Disabled,
        false,
        5000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(descriptor, a_assertContext);

    if (!backendResult)
    {
        return a_policy == cue::D3d12AdapterPolicy::HighPerformanceHardware ? 77 : 3;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    const cue::CapabilityReport &capabilities = backend->capabilities();

    if (backend->state() != cue::GraphicsBackendState::Ready || capabilities.adapterName.empty() ||
        capabilities.backendKind != cue::GraphicsBackendKind::D3d12 ||
        capabilities.adapterKind != a_expectedKind || capabilities.profile != cue::GraphicsProfile::Baseline3D)
    {
        static_cast<void>(backend->shutdown());
        return 4;
    }

    cue::Result<void> firstShutdownResult = backend->shutdown();

    if (!firstShutdownResult || backend->state() != cue::GraphicsBackendState::Shutdown)
    {
        return 5;
    }

    cue::Result<void> secondShutdownResult = backend->shutdown();

    if (!secondShutdownResult)
    {
        return 6;
    }

    backend.reset();
    return 0;
}

/// @brief D3d12BackendProcess Test の Backend Thread DestructionScenario を実行し、検証結果を返す
[[nodiscard]] int run_backend_thread_destruction(cue::AssertContext &a_assertContext)
{
    cue::D3d12BackendDescriptor descriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::D3d12ValidationMode::Disabled,
        false,
        5000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(descriptor, a_assertContext);

    if (!backendResult)
    {
        return 8;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());

    if (!backend->shutdown())
    {
        return 9;
    }

#if CUE_ENABLE_ASSERTS
    std::thread invalidThread(
        [backend = std::move(backend)]() mutable { backend.reset(); });
    invalidThread.join();
    return 10;
#else
    backend.reset();
    return 0;
#endif
}

/// @brief D3d12BackendProcess Test の Invalid Wait TimeoutScenario を実行し、検証結果を返す
[[nodiscard]] int run_invalid_wait_timeout(cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12BackendDescriptor descriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::D3d12ValidationMode::Disabled,
        false,
        0,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> result =
        cue::create_d3d12_backend(descriptor, a_assertContext);
    return !result && result.try_error() != nullptr &&
                   result.try_error()->code().domain() == "Cue.RHI.D3D12"
               ? 0
               : 11;
}
} // namespace

int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    std::string_view mode = a_arguments[1];

    if (mode != "Hardware" && mode != "Warp" && mode != "DeviceFailure" &&
        mode != "ThreadDestruction" && mode != "InvalidWaitTimeout")
    {
        return 2;
    }

    ProcessFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<ProcessLogSink>());
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);

    if (mode == "DeviceFailure")
    {
        return cue::verify_d3d12_device_creation_failure_for_probe(assertContext) ? 0 : 7;
    }

    if (mode == "ThreadDestruction")
    {
        return run_backend_thread_destruction(assertContext);
    }

    if (mode == "InvalidWaitTimeout")
    {
        return run_invalid_wait_timeout(assertContext);
    }

    cue::D3d12AdapterPolicy policy = mode == "Warp" ? cue::D3d12AdapterPolicy::Warp
                                                     : cue::D3d12AdapterPolicy::HighPerformanceHardware;
    cue::GraphicsAdapterKind expectedKind = mode == "Warp" ? cue::GraphicsAdapterKind::Software
                                                            : cue::GraphicsAdapterKind::Hardware;
    return run_backend_lifecycle(policy, expectedKind, assertContext);
}
