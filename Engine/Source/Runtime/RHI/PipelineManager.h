#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct GraphicsPipelineStateDesc
    {
        std::string_view name;
    };

    struct RootSignatureDesc
    {
        std::string_view name;
    };

    struct ShaderBlobDesc
    {
        std::string_view name;
    };

    class IPipelineManager
    {
    public:
        IPipelineManager() = default;
        // コピー禁止
        IPipelineManager(const IPipelineManager&) = delete;
        IPipelineManager& operator=(const IPipelineManager&) = delete;
        // ムーブ禁止
        IPipelineManager(IPipelineManager&&) = delete;
        IPipelineManager& operator=(IPipelineManager&&) = delete;
        virtual ~IPipelineManager() = default;

        virtual Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineHandle& out) = 0;
        virtual Result destroy_graphics_pipeline(PipelineHandle handle) = 0;

        virtual Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out) = 0;
        virtual Result destroy_root_signature(RootSignatureHandle handle) = 0;

        virtual Result create_shader_blob(const ShaderBlobDesc& desc, ShaderBlobHandle& out) = 0;
        virtual Result destroy_shader_blob(ShaderBlobHandle handle) = 0;
    };
}
