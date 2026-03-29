#pragma once

// === RHI Includes ===
#include <PipelineManager.h>
#include "HLSLCompiler.h"
#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    struct DX12GraphicsPipelineRecord final
    {
        GraphicsPipelineStateDesc desc; // グラフィックスパイプラインステートの記述
        // グラフィックスパイプラインステートオブジェクトの実体
        ComPtr<ID3D12PipelineState> pipelineState;
    };

    struct DX12ComputePipelineRecord final
    {
        ComputePipelineStateDesc desc;
        ComPtr<ID3D12PipelineState> pipelineState;
    };

    class DX12PipelineManager final : public IPipelineManager
    {
    public:
        DX12PipelineManager(DX12RenderDevice& renderDevice, HLSLCompiler& hlslCompiler) : m_renderDevice(renderDevice), m_hlslCompiler(hlslCompiler) {}
        ~DX12PipelineManager() override = default;
        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out) override;
        Result destroy_graphics_pipeline(PipelineStateHandle handle) override;
        Result create_compute_pipeline(const ComputePipelineStateDesc& desc, PipelineStateHandle& out) override;
        Result destroy_compute_pipeline(PipelineStateHandle handle) override;
        Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out) override;
        Result destroy_root_signature(RootSignatureHandle handle) override;
        Result create_shader_blob(const ShaderCompileDesc& desc, ShaderBlobHandle& out) override;
        Result destroy_shader_blob(ShaderBlobHandle handle) override;
        bool try_get_graphics_pipeline(PipelineStateHandle handle, const DX12GraphicsPipelineRecord*& outRecord) const;
        bool try_get_compute_pipeline(PipelineStateHandle handle, const DX12ComputePipelineRecord*& outRecord) const;
        bool try_get_root_signature(RootSignatureHandle handle, ComPtr<ID3D12RootSignature>& outRootSignature) const;
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        HLSLCompiler& m_hlslCompiler;
        Registry<PipelineTag, DX12GraphicsPipelineRecord> m_pipelineRegistry; // グラフィックスパイプラインステートのレジストリ
        std::unordered_map<Core::ResourceNameId, PipelineStateHandle> m_pipelineNameMap; // 名前からグラフィックスパイプラインステートハンドルへのマッピング
        Registry<PipelineTag, DX12ComputePipelineRecord> m_computePipelineRegistry;
        std::unordered_map<Core::ResourceNameId, PipelineStateHandle> m_computePipelineNameMap;
        Registry<RootSignatureTag, ComPtr<ID3D12RootSignature>> m_rootSignatureRegistry; // ルートシグネチャのレジストリ
        std::unordered_map<Core::ResourceNameId, RootSignatureHandle> m_rootSignatureNameMap; // 名前からルートシグネチャハンドルへのマッピング
        Registry<ShaderBlobTag, ComPtr<IDxcBlob>> m_shaderBlobRegistry; // シェーダーブロブのレジストリ
        std::unordered_map<Core::ResourceNameId, ShaderBlobHandle> m_shaderBlobNameMap; // 名前からシェーダーブロブハンドルへのマッピング
    };
}
