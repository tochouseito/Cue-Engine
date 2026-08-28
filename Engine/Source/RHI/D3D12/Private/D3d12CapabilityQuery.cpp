#include "D3d12CapabilityQuery.h"

#include "D3d12Error.h"

#include <Cue/Foundation/Assert.h>

#include <array>
#include <optional>
#include <utility>

namespace
{
constexpr std::int64_t k_optionalCapabilityQueryFailed = 100;
constexpr std::int64_t k_capabilityDiagnosticDeliveryFailed = 101;
thread_local std::optional<D3D12_FEATURE> g_failureFeature;

/// @brief Production Queryを呼び、Probe指定FeatureだけSynthetic失敗へ差し替える
HRESULT check_feature_support(ID3D12Device *a_device, D3D12_FEATURE a_feature, void *a_data,
                              UINT a_dataSize) noexcept
{
    if (g_failureFeature && *g_failureFeature == a_feature)
    {
        return E_FAIL;
    }
    return a_device->CheckFeatureSupport(a_feature, a_data, a_dataSize);
}

/// @brief Optional Query失敗をLogし、配送失敗時だけBackend生成を止めるErrorを返す
[[nodiscard]] cue::Result<void> report_optional_query_failure(
    HRESULT a_nativeCode, const char *a_summary, const cue::AssertContext &a_assertContext) noexcept
{
    cue::Error queryError = cue::d3d12_private::make_native_error(
        a_assertContext, k_optionalCapabilityQueryFailed, a_summary, a_nativeCode);
    const cue::LogResult logResult =
        a_assertContext.logger().log(cue::LogLevel::Warning, a_summary, std::move(queryError));
    if (logResult == cue::LogResult::Success)
    {
        return cue::Result<void>::success();
    }

    cue::Error cause = cue::d3d12_private::make_native_error(
        a_assertContext, k_optionalCapabilityQueryFailed, a_summary, a_nativeCode);
    cue::Error deliveryError = cue::Error::reclassify(
        a_assertContext.fatal_handler(),
        cue::d3d12_private::make_code(a_assertContext, k_capabilityDiagnosticDeliveryFailed),
        "D3D12 optional capability diagnostic delivery failed", std::move(cause));
    return cue::Result<void>::failure(std::move(deliveryError));
}

/// @brief BOOL Feature値をSupport状態へ変換する
[[nodiscard]] constexpr cue::CapabilitySupportState map_boolean(BOOL a_value) noexcept
{
    return a_value != FALSE ? cue::CapabilitySupportState::supported()
                            : cue::CapabilitySupportState::unsupported();
}

/// @brief D3D12 Feature LevelをPlatform非依存値へ変換する
[[nodiscard]] constexpr std::optional<cue::GraphicsFeatureLevel> map_feature_level(
    D3D_FEATURE_LEVEL a_value) noexcept
{
    switch (a_value)
    {
    case D3D_FEATURE_LEVEL_12_0:
        return cue::GraphicsFeatureLevel::Level12_0;
    case D3D_FEATURE_LEVEL_12_1:
        return cue::GraphicsFeatureLevel::Level12_1;
    case D3D_FEATURE_LEVEL_12_2:
        return cue::GraphicsFeatureLevel::Level12_2;
    default:
        return std::nullopt;
    }
}

/// @brief D3D Shader ModelをMajor・Minor Versionへ変換する
[[nodiscard]] constexpr std::optional<cue::CapabilityVersion> map_shader_model(D3D_SHADER_MODEL a_value) noexcept
{
    switch (a_value)
    {
    case D3D_SHADER_MODEL_5_1:
        return cue::CapabilityVersion{5, 1};
    case D3D_SHADER_MODEL_6_0:
        return cue::CapabilityVersion{6, 0};
    case D3D_SHADER_MODEL_6_1:
        return cue::CapabilityVersion{6, 1};
    case D3D_SHADER_MODEL_6_2:
        return cue::CapabilityVersion{6, 2};
    case D3D_SHADER_MODEL_6_3:
        return cue::CapabilityVersion{6, 3};
    case D3D_SHADER_MODEL_6_4:
        return cue::CapabilityVersion{6, 4};
    case D3D_SHADER_MODEL_6_5:
        return cue::CapabilityVersion{6, 5};
    case D3D_SHADER_MODEL_6_6:
        return cue::CapabilityVersion{6, 6};
    case D3D_SHADER_MODEL_6_7:
        return cue::CapabilityVersion{6, 7};
    case D3D_SHADER_MODEL_6_8:
        return cue::CapabilityVersion{6, 8};
    default:
        return std::nullopt;
    }
}

/// @brief Native Root Signature VersionをMajor・Minor Versionへ変換する
[[nodiscard]] constexpr std::optional<cue::CapabilityVersion> map_root_signature(
    D3D_ROOT_SIGNATURE_VERSION a_value) noexcept
{
    switch (a_value)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        return cue::CapabilityVersion{1, 0};
    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        return cue::CapabilityVersion{1, 1};
    default:
        return std::nullopt;
    }
}

/// @brief Native Resource Binding TierをPlatform非依存Tierへ変換する
[[nodiscard]] constexpr std::optional<cue::ResourceBindingTier> map_binding_tier(
    D3D12_RESOURCE_BINDING_TIER a_value) noexcept
{
    switch (a_value)
    {
    case D3D12_RESOURCE_BINDING_TIER_1:
        return cue::ResourceBindingTier::Tier1;
    case D3D12_RESOURCE_BINDING_TIER_2:
        return cue::ResourceBindingTier::Tier2;
    case D3D12_RESOURCE_BINDING_TIER_3:
        return cue::ResourceBindingTier::Tier3;
    default:
        return std::nullopt;
    }
}

/// @brief Native Resource Heap TierをPlatform非依存Tierへ変換する
[[nodiscard]] constexpr std::optional<cue::ResourceHeapTier> map_heap_tier(
    D3D12_RESOURCE_HEAP_TIER a_value) noexcept
{
    switch (a_value)
    {
    case D3D12_RESOURCE_HEAP_TIER_1:
        return cue::ResourceHeapTier::Tier1;
    case D3D12_RESOURCE_HEAP_TIER_2:
        return cue::ResourceHeapTier::Tier2;
    default:
        return std::nullopt;
    }
}

/// @brief Native Ray Tracing TierをUnsupportedまたはPlatform非依存Tierへ変換する
[[nodiscard]] cue::GraphicsCapabilityValue<cue::RayTracingTier> map_ray_tracing(
    D3D12_RAYTRACING_TIER a_value) noexcept
{
    switch (a_value)
    {
    case D3D12_RAYTRACING_TIER_NOT_SUPPORTED:
        return cue::GraphicsCapabilityValue<cue::RayTracingTier>::unsupported();
    case D3D12_RAYTRACING_TIER_1_0:
        return cue::GraphicsCapabilityValue<cue::RayTracingTier>::supported(cue::RayTracingTier::Tier1_0);
    case D3D12_RAYTRACING_TIER_1_1:
        return cue::GraphicsCapabilityValue<cue::RayTracingTier>::supported(cue::RayTracingTier::Tier1_1);
    default:
        return cue::GraphicsCapabilityValue<cue::RayTracingTier>::query_failed();
    }
}

/// @brief Native Mesh Shader TierをUnsupportedまたはPlatform非依存Tierへ変換する
[[nodiscard]] cue::GraphicsCapabilityValue<cue::MeshShaderTier> map_mesh_shader(
    D3D12_MESH_SHADER_TIER a_value) noexcept
{
    switch (a_value)
    {
    case D3D12_MESH_SHADER_TIER_NOT_SUPPORTED:
        return cue::GraphicsCapabilityValue<cue::MeshShaderTier>::unsupported();
    case D3D12_MESH_SHADER_TIER_1:
        return cue::GraphicsCapabilityValue<cue::MeshShaderTier>::supported(cue::MeshShaderTier::Tier1);
    default:
        return cue::GraphicsCapabilityValue<cue::MeshShaderTier>::query_failed();
    }
}

/// @brief Native VRS TierをUnsupportedまたはPlatform非依存Tierへ変換する
[[nodiscard]] cue::GraphicsCapabilityValue<cue::VariableRateShadingTier> map_vrs(
    D3D12_VARIABLE_SHADING_RATE_TIER a_value) noexcept
{
    switch (a_value)
    {
    case D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED:
        return cue::GraphicsCapabilityValue<cue::VariableRateShadingTier>::unsupported();
    case D3D12_VARIABLE_SHADING_RATE_TIER_1:
        return cue::GraphicsCapabilityValue<cue::VariableRateShadingTier>::supported(
            cue::VariableRateShadingTier::Tier1);
    case D3D12_VARIABLE_SHADING_RATE_TIER_2:
        return cue::GraphicsCapabilityValue<cue::VariableRateShadingTier>::supported(
            cue::VariableRateShadingTier::Tier2);
    default:
        return cue::GraphicsCapabilityValue<cue::VariableRateShadingTier>::query_failed();
    }
}

/// @brief Native Sampler Feedback TierをUnsupportedまたはPlatform非依存Tierへ変換する
[[nodiscard]] cue::GraphicsCapabilityValue<cue::SamplerFeedbackTier> map_sampler_feedback(
    D3D12_SAMPLER_FEEDBACK_TIER a_value) noexcept
{
    switch (a_value)
    {
    case D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED:
        return cue::GraphicsCapabilityValue<cue::SamplerFeedbackTier>::unsupported();
    case D3D12_SAMPLER_FEEDBACK_TIER_0_9:
        return cue::GraphicsCapabilityValue<cue::SamplerFeedbackTier>::supported(
            cue::SamplerFeedbackTier::Tier0_9);
    case D3D12_SAMPLER_FEEDBACK_TIER_1_0:
        return cue::GraphicsCapabilityValue<cue::SamplerFeedbackTier>::supported(
            cue::SamplerFeedbackTier::Tier1_0);
    default:
        return cue::GraphicsCapabilityValue<cue::SamplerFeedbackTier>::query_failed();
    }
}

/// @brief Optional Query失敗時にFieldをFailedへ保ったまま診断配送結果を確認する
[[nodiscard]] cue::Result<void> handle_failure(HRESULT a_result, const char *a_summary,
                                                const cue::AssertContext &a_context) noexcept
{
    return report_optional_query_failure(a_result, a_summary, a_context);
}
} // namespace

namespace cue
{
Result<CapabilityReport> query_d3d12_capability_report(
    ID3D12Device *a_device, const D3d12AdapterReport &a_adapterReport,
    const AssertContext &a_assertContext) noexcept
{
    Result<CapabilityReport> baseResult = make_d3d12_capability_report(a_adapterReport, a_assertContext);
    if (!baseResult)
    {
        return Result<CapabilityReport>::failure(std::move(*baseResult.try_error()));
    }
    CapabilityReport report = std::move(*baseResult.try_value());

    std::array<D3D_FEATURE_LEVEL, 3> requestedLevels = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {
        static_cast<UINT>(requestedLevels.size()), requestedLevels.data(), D3D_FEATURE_LEVEL_12_0};
    HRESULT result = check_feature_support(a_device, D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));
    if (SUCCEEDED(result))
    {
        const std::optional<GraphicsFeatureLevel> value = map_feature_level(levels.MaxSupportedFeatureLevel);
        report.featureLevel = value ? GraphicsCapabilityValue<GraphicsFeatureLevel>::supported(*value)
                                    : GraphicsCapabilityValue<GraphicsFeatureLevel>::query_failed();
    }
    else
    {
        report.featureLevel = GraphicsCapabilityValue<GraphicsFeatureLevel>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 feature level query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    constexpr std::array requestedShaderModels = {
        D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
    };
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {};
    for (const D3D_SHADER_MODEL requestedShaderModel : requestedShaderModels)
    {
        shaderModel.HighestShaderModel = requestedShaderModel;
        result = check_feature_support(a_device, D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
        if (result != E_INVALIDARG)
        {
            break;
        }
    }
    if (SUCCEEDED(result))
    {
        const std::optional<CapabilityVersion> value = map_shader_model(shaderModel.HighestShaderModel);
        report.shaderModel = value ? GraphicsCapabilityValue<CapabilityVersion>::supported(*value)
                                   : GraphicsCapabilityValue<CapabilityVersion>::query_failed();
    }
    else
    {
        report.shaderModel = GraphicsCapabilityValue<CapabilityVersion>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 shader model query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature = {D3D_ROOT_SIGNATURE_VERSION_1_1};
    result = check_feature_support(a_device, D3D12_FEATURE_ROOT_SIGNATURE, &rootSignature, sizeof(rootSignature));
    if (SUCCEEDED(result))
    {
        const std::optional<CapabilityVersion> value = map_root_signature(rootSignature.HighestVersion);
        report.rootSignature = value ? GraphicsCapabilityValue<CapabilityVersion>::supported(*value)
                                     : GraphicsCapabilityValue<CapabilityVersion>::query_failed();
    }
    else if (result == E_INVALIDARG)
    {
        // Root Signature 1.1を認識しないRuntimeでもD3D12 Baselineの1.0は利用できる
        report.rootSignature =
            GraphicsCapabilityValue<CapabilityVersion>::supported(CapabilityVersion{1, 0});
    }
    else
    {
        report.rootSignature = GraphicsCapabilityValue<CapabilityVersion>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 root signature query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    result = check_feature_support(a_device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (SUCCEEDED(result))
    {
        const auto binding = map_binding_tier(options.ResourceBindingTier);
        const auto heap = map_heap_tier(options.ResourceHeapTier);
        report.resourceBinding = binding ? GraphicsCapabilityValue<ResourceBindingTier>::supported(*binding)
                                         : GraphicsCapabilityValue<ResourceBindingTier>::query_failed();
        report.resourceHeap = heap ? GraphicsCapabilityValue<ResourceHeapTier>::supported(*heap)
                                   : GraphicsCapabilityValue<ResourceHeapTier>::query_failed();
    }
    else
    {
        report.resourceBinding = GraphicsCapabilityValue<ResourceBindingTier>::query_failed();
        report.resourceHeap = GraphicsCapabilityValue<ResourceHeapTier>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 resource tier query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    result = check_feature_support(a_device, D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
    if (SUCCEEDED(result))
    {
        report.waveOperations = map_boolean(options1.WaveOps);
    }
    else
    {
        report.waveOperations = CapabilitySupportState::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 wave operations query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    result = check_feature_support(a_device, D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (SUCCEEDED(result))
    {
        report.rayTracing = map_ray_tracing(options5.RaytracingTier);
    }
    else
    {
        report.rayTracing = GraphicsCapabilityValue<RayTracingTier>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 ray tracing query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
    result = check_feature_support(a_device, D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6));
    if (SUCCEEDED(result))
    {
        report.variableRateShading = map_vrs(options6.VariableShadingRateTier);
    }
    else
    {
        report.variableRateShading = GraphicsCapabilityValue<VariableRateShadingTier>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 variable rate shading query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    result = check_feature_support(a_device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    if (SUCCEEDED(result))
    {
        report.meshShader = map_mesh_shader(options7.MeshShaderTier);
        report.samplerFeedback = map_sampler_feedback(options7.SamplerFeedbackTier);
    }
    else
    {
        report.meshShader = GraphicsCapabilityValue<MeshShaderTier>::query_failed();
        report.samplerFeedback = GraphicsCapabilityValue<SamplerFeedbackTier>::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 mesh and sampler feedback query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
    result = check_feature_support(a_device, D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12));
    if (SUCCEEDED(result))
    {
        report.enhancedBarriers = map_boolean(options12.EnhancedBarriersSupported);
    }
    else
    {
        report.enhancedBarriers = CapabilitySupportState::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 enhanced barriers query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture = {};
    result = check_feature_support(a_device, D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture));
    if (SUCCEEDED(result))
    {
        report.uma = map_boolean(architecture.UMA);
        report.cacheCoherentUma = map_boolean(architecture.CacheCoherentUMA);
    }
    else
    {
        report.uma = CapabilitySupportState::query_failed();
        report.cacheCoherentUma = CapabilitySupportState::query_failed();
        Result<void> failure = handle_failure(result, "D3D12 architecture query failed", a_assertContext);
        if (!failure)
        {
            return Result<CapabilityReport>::failure(std::move(*failure.try_error()));
        }
    }

    return Result<CapabilityReport>::success(std::move(report));
}

namespace d3d12_private
{
void set_capability_query_failure_for_probe(D3D12_FEATURE a_feature) noexcept
{
    g_failureFeature = a_feature;
}

void clear_capability_query_failure_for_probe() noexcept
{
    g_failureFeature.reset();
}
} // namespace d3d12_private
} // namespace cue
