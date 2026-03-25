#include "DX12PipelineManager.h"

namespace Cue::RHI::DX12
{
    Result DX12PipelineManager::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineHandle& out)
    {
        desc;
        out;
        return Result();
    }
    Result DX12PipelineManager::destroy_graphics_pipeline(PipelineHandle handle)
    {
        handle;
        return Result();
    }
    Result DX12PipelineManager::create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out)
    {
        desc;
        out;
        return Result();
    }
    Result DX12PipelineManager::destroy_root_signature(RootSignatureHandle handle)
    {
        handle;
        return Result();
    }
    Result DX12PipelineManager::create_shader_blob(const ShaderBlobDesc& desc, ShaderBlobHandle& out)
    {
        desc;
        out;
        return Result();
    }
    Result DX12PipelineManager::destroy_shader_blob(ShaderBlobHandle handle)
    {
        handle;
        return Result();
    }
}
