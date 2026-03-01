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

        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& outHandle) override;
        Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& outHandle) override;
        Result compile_shader(const ShaderCompileDesc& desc, ShaderBlobHandle& outHandle) override;
    private:
        RenderDevice& m_renderDevice; // RenderDeviceへの参照
        HLSLCompiler& m_shaderCompiler; // HLSLCompilerへの参照
        Registry<PipelineStateTag, ComPtr<ID3D12PipelineState>> m_pipelineStateRegistry;
        Registry<RootSignatureTag, ComPtr<ID3D12RootSignature>> m_rootSignatureRegistry;
        Registry<ShaderBlobTag, ComPtr<IDxcBlob>> m_shaderBlobRegistry;
    };
} // namespace Cue::GraphicsCore::DX12
