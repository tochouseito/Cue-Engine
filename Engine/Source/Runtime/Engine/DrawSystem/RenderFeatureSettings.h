#pragma once

/// ****************************************************************************
/// Render feature switches for benchmark comparisons
/// ****************************************************************************

namespace Cue::DrawSystem
{
    enum class RenderComparisonMode : int
    {
        Unoptimized = 0,
        GpuCulling,
        Lod,
        HiZ,
        Batching,
        Final,
    };

    struct RenderFeatureSettings final
    {
        RenderComparisonMode mode = RenderComparisonMode::Final;
        bool gpuCullingEnabled = true;
        bool lodEnabled = true;
        bool hiZEnabled = true;
        bool batchingEnabled = true;
    };

    [[nodiscard]] inline RenderFeatureSettings
    render_feature_settings_for_mode(RenderComparisonMode mode) noexcept
    {
        RenderFeatureSettings settings{};
        settings.mode = mode;

        switch (mode)
        {
        case RenderComparisonMode::Unoptimized:
            settings.gpuCullingEnabled = false;
            settings.lodEnabled = false;
            settings.hiZEnabled = false;
            settings.batchingEnabled = false;
            break;
        case RenderComparisonMode::GpuCulling:
            settings.gpuCullingEnabled = true;
            settings.lodEnabled = false;
            settings.hiZEnabled = false;
            settings.batchingEnabled = false;
            break;
        case RenderComparisonMode::Lod:
            settings.gpuCullingEnabled = true;
            settings.lodEnabled = true;
            settings.hiZEnabled = false;
            settings.batchingEnabled = false;
            break;
        case RenderComparisonMode::HiZ:
            settings.gpuCullingEnabled = true;
            settings.lodEnabled = true;
            settings.hiZEnabled = true;
            settings.batchingEnabled = false;
            break;
        case RenderComparisonMode::Batching:
        case RenderComparisonMode::Final:
            settings.gpuCullingEnabled = true;
            settings.lodEnabled = true;
            settings.hiZEnabled = true;
            settings.batchingEnabled = true;
            break;
        default:
            break;
        }

        return settings;
    }

    [[nodiscard]] inline const char*
    render_comparison_mode_label(RenderComparisonMode mode) noexcept
    {
        switch (mode)
        {
        case RenderComparisonMode::Unoptimized:
            return "Before optimization";
        case RenderComparisonMode::GpuCulling:
            return "GPU Culling";
        case RenderComparisonMode::Lod:
            return "LOD";
        case RenderComparisonMode::HiZ:
            return "Hi-Z";
        case RenderComparisonMode::Batching:
            return "Batching";
        case RenderComparisonMode::Final:
            return "Final";
        default:
            return "Unknown";
        }
    }
} // namespace Cue::DrawSystem
