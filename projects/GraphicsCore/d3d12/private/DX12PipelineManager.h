#pragma once
#include "stdafx.h"
#include "RenderDevice.h"
#include "HLSLCompiler.h"
#include <PipelineManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12PipelineManager final : public PipelineManager
    {
    public:
        DX12PipelineManager(RenderDevice& renderDevice, HLSLCompiler& shaderCompiler)
            : m_renderDevice(renderDevice), m_shaderCompiler(shaderCompiler) {}
        ~DX12PipelineManager() override = default;

        PipelineStateHandle create_graphics_pipeline(const GraphicsPipelineStateDesc& desc) override;

        ShaderBlobHandle compile_shader(const ShaderCompileDesc& desc) override;
    private:
        RenderDevice& m_renderDevice; // RenderDeviceへの参照
        HLSLCompiler& m_shaderCompiler; // HLSLCompilerへの参照
        Registry<PipelineStateTag, ComPtr<ID3D12PipelineState>> m_pipelineStateRegistry;
        Registry<RootSignatureTag, ComPtr<ID3D12RootSignature>> m_rootSignatureRegistry;
        Registry<ShaderBlobTag, ComPtr<IDxcBlob>> m_shaderBlobRegistry;
    };
} // namespace Cue::GraphicsCore::DX12
