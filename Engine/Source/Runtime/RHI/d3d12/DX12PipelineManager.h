#pragma once

// === RHI Includes ===
#include <PipelineManager.h>

namespace Cue::RHI::DX12
{
    class DX12PipelineManager final : public IPipelineManager
    {
    public:
        DX12PipelineManager() = default;
        ~DX12PipelineManager() override = default;
        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineHandle& out) override;
        Result destroy_graphics_pipeline(PipelineHandle handle) override;
        Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out) override;
        Result destroy_root_signature(RootSignatureHandle handle) override;
        Result create_shader_blob(const ShaderBlobDesc& desc, ShaderBlobHandle& out) override;
        Result destroy_shader_blob(ShaderBlobHandle handle) override;
    };
}
