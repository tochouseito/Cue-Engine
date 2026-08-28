#include "TestSupport/RhiProcessTestFixture.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Log.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12AdapterSelectionProbe.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12BackendProbe.h>

#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
class CapabilityWarningFailingLogSink final : public cue::LogSink
{
  public:
    /// @brief Optional Capability Query失敗Warningだけを配送失敗させる
    [[nodiscard]] bool write(const cue::LogRecord &a_record) override
    {
        return !(a_record.level() == cue::LogLevel::Warning &&
                 a_record.message() == "D3D12 mesh and sampler feedback query failed");
    }

    /// @brief 保留中の出力を持たないためFlush成功を返す
    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

/// @brief D3d12BackendProcess Test の Backend LifecycleScenario を実行し、検証結果を返す
[[nodiscard]] int run_backend_lifecycle(cue::D3d12AdapterPolicy a_policy, cue::GraphicsAdapterKind a_expectedKind,
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

    const auto wasQueried = [](cue::CapabilitySupportState a_state) noexcept
    {
        return a_state.query_status() != cue::CapabilityQueryStatus::NotQueried;
    };

    if (backend->state() != cue::GraphicsBackendState::Ready || capabilities.adapterName.empty() ||
        capabilities.backendKind != cue::GraphicsBackendKind::D3d12 || capabilities.adapterKind != a_expectedKind ||
        capabilities.profile != cue::GraphicsProfile::Baseline3D ||
        !wasQueried(capabilities.featureLevel.support_state()) ||
        !wasQueried(capabilities.shaderModel.support_state()) ||
        !wasQueried(capabilities.rootSignature.support_state()) ||
        !wasQueried(capabilities.resourceBinding.support_state()) ||
        !wasQueried(capabilities.resourceHeap.support_state()) ||
        !wasQueried(capabilities.rayTracing.support_state()) || !wasQueried(capabilities.meshShader.support_state()) ||
        !wasQueried(capabilities.variableRateShading.support_state()) ||
        !wasQueried(capabilities.samplerFeedback.support_state()) || !wasQueried(capabilities.waveOperations) ||
        !wasQueried(capabilities.enhancedBarriers) || !wasQueried(capabilities.uma) ||
        !wasQueried(capabilities.cacheCoherentUma))
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
    /// @brief 所有 Thread 外で Backend を破棄し、Thread Affinity Assert が発火することを検証する
    std::thread invalidThread([backend = std::move(backend)]() mutable { backend.reset(); });
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
    cue::Result<std::unique_ptr<cue::D3d12Backend>> result = cue::create_d3d12_backend(descriptor, a_assertContext);
    return !result && result.try_error() != nullptr && result.try_error()->code().domain() == "Cue.RHI.D3D12" ? 0 : 11;
}
} // namespace

/// @brief 指定 Scenario で D3D12 Backend の所有権と Thread Affinity を Process 単位で検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    std::string_view mode = a_arguments[1];

    if (mode != "Hardware" && mode != "Warp" && mode != "DeviceFailure" && mode != "ThreadDestruction" &&
        mode != "InvalidWaitTimeout" && mode != "OptionalCapabilityFailure" &&
        mode != "OptionalCapabilityLogFailure")
    {
        return 2;
    }

    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    if (mode == "OptionalCapabilityLogFailure")
    {
        sinks.push_back(std::make_unique<CapabilityWarningFailingLogSink>());
    }
    cue::test::RhiProcessTestFixture fixture(std::move(sinks));
    cue::AssertContext &assertContext = fixture.assert_context();

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

    if (mode == "OptionalCapabilityFailure")
    {
        return cue::verify_d3d12_optional_capability_failure_for_probe(assertContext) ? 0 : 12;
    }

    if (mode == "OptionalCapabilityLogFailure")
    {
        return cue::verify_d3d12_optional_capability_log_failure_for_probe(assertContext) ? 0 : 13;
    }

    cue::D3d12AdapterPolicy policy =
        mode == "Warp" ? cue::D3d12AdapterPolicy::Warp : cue::D3d12AdapterPolicy::HighPerformanceHardware;
    cue::GraphicsAdapterKind expectedKind =
        mode == "Warp" ? cue::GraphicsAdapterKind::Software : cue::GraphicsAdapterKind::Hardware;
    return run_backend_lifecycle(policy, expectedKind, assertContext);
}
