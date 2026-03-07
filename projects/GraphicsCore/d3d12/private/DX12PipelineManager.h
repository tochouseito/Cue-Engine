#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "HLSLCompiler.h"
#include "DescriptorAllocator.h"
#include <PipelineManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12PipelineManager final : public IPipelineManager
    {
    public:
        DX12PipelineManager(DX12RenderDevice& renderDevice, HLSLCompiler& shaderCompiler, DescriptorAllocator& descriptorAllocator)
            : m_renderDevice(renderDevice), m_shaderCompiler(shaderCompiler), m_descriptorAllocator(descriptorAllocator)
        {

        }

        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& outHandle) override;
        Result destroy_pipeline(const PipelineStateHandle& handle) override;
        Result get_pipeline(ResourceNameId nameId, PipelineStateHandle& outHandle) override;

        Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& outHandle) override;
        Result destroy_root_signature(const RootSignatureHandle& handle) override;
        Result get_root_signature(ResourceNameId nameId, RootSignatureHandle& outHandle) override;

        Result compile_shader(const ShaderCompileDesc& desc, ShaderBlobHandle& outHandle) override;
        Result destroy_shader(const ShaderBlobHandle& handle) override;
        Result get_shader(ResourceNameId nameId, ShaderBlobHandle& outHandle) override;
        Result resolve_pipeline_state(PipelineStateHandle pipelineHandle, ID3D12PipelineState*& outPipelineState) const;
        Result resolve_root_signature(RootSignatureHandle rootSignatureHandle, ID3D12RootSignature*& outRootSignature) const;
    private:
        DX12RenderDevice& m_renderDevice; // RenderDeviceへの参照
        HLSLCompiler& m_shaderCompiler; // HLSLCompilerへの参照
        DescriptorAllocator& m_descriptorAllocator; // DescriptorAllocatorへの参照
        Registry<PipelineStateTag, ComPtr<ID3D12PipelineState>> m_pipelineRegistry;
        std::unordered_map<ResourceNameId, PipelineStateHandle> m_pipelineNameMap;
        Registry<RootSignatureTag, ComPtr<ID3D12RootSignature>> m_rootSignatureRegistry;
        std::unordered_map<ResourceNameId, RootSignatureHandle> m_rootSignatureNameMap;
        Registry<ShaderBlobTag, ComPtr<IDxcBlob>> m_shaderBlobRegistry;
        std::unordered_map<ResourceNameId, ShaderBlobHandle> m_shaderBlobNameMap;
    };
} // namespace Cue::GraphicsCore::DX12
