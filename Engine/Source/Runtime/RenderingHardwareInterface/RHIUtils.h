#pragma once

/// ************************************************************************************
/// RHI ヘルパー
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

// === RHI includes ===
#include "RHI.h"
#include "RHICommon.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"

// === C++ includes ===
#include <string_view>

namespace Cue::RHI
{
    struct RenderTargetResources final
    {
        RHI::TextureHandle colorHandle{};
        RHI::ViewHandle colorRtvHandle{};
        RHI::ViewHandle colorSrvHandle{};
    };

    Result create_render_target_resources(
        IRenderBackend& a_backend,
        std::string_view a_name,
        RHI::ColorFormat a_format,
        RenderTargetResources& a_outResources,
        const float* a_clearColor);
    Result destroy_render_target_resources(
        IRenderBackend& a_backend,
        RenderTargetResources& a_resources);
}
