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
        comPtr<ID3D12PipelineState> pipelineState; // グラフィックスパイプラインステートオブジェクトの実体
    };

    struct DX12ComputePipelineRecord final
    {
        ComputePipelineStateDesc desc; // コンピュートパイプラインステートの記述
        comPtr<ID3D12PipelineState> pipelineState; // コンピュートパイプラインステートオブジェクトの実体
    };

    struct RootSignatureRecord final
    {
        RootSignatureDesc desc; // ルートシグネチャの記述
        comPtr<ID3D12RootSignature> rootSignature; // ルートシグネチャの実体
    };

    struct ShaderBlobRecord final
    {
        ShaderCompileDesc desc; // シェーダーブロブの記述
        comPtr<IDxcBlob> shaderBlob; // シェーダーブロブの実体
    };

    class DX12PipelineManager final : public IPipelineManager
    {
    public:
        DX12PipelineManager(DX12RenderDevice& renderDevice, HLSLCompiler& hlslCompiler) : m_renderDevice(renderDevice), m_hlslCompiler(hlslCompiler) {}
        ~DX12PipelineManager() override = default;
        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, pipelineStateHandle& out) override;
        Result destroy_graphics_pipeline(pipelineStateHandle handle) override;
        Result get_graphics_pipeline(std::string_view name, pipelineStateHandle& out) override;
        Result create_compute_pipeline(const ComputePipelineStateDesc& desc, pipelineStateHandle& out) override;
        Result destroy_compute_pipeline(pipelineStateHandle handle) override;
        Result get_compute_pipeline(std::string_view name, pipelineStateHandle& out) override;
        Result create_root_signature(const RootSignatureDesc& desc, rootSignatureHandle& out) override;
        Result destroy_root_signature(rootSignatureHandle handle) override;
        Result get_root_signature(std::string_view name, rootSignatureHandle& out) override;
        Result create_shader_blob(const ShaderCompileDesc& desc, shaderBlobHandle& out) override;
        Result destroy_shader_blob(shaderBlobHandle handle) override;
        Result get_shader_blob(std::string_view name, shaderBlobHandle& out) override;
        bool try_get_graphics_pipeline(pipelineStateHandle handle, DX12GraphicsPipelineRecord** outRecord);
        bool try_get_compute_pipeline(pipelineStateHandle handle, DX12ComputePipelineRecord** outRecord);
        bool try_get_root_signature(rootSignatureHandle handle, RootSignatureRecord** outRecord);
        bool try_get_shader_blob(shaderBlobHandle handle, ShaderBlobRecord** outRecord);
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        HLSLCompiler& m_hlslCompiler;
        Core::Registry<PipelineTag, DX12GraphicsPipelineRecord> m_pipelineRegistry; // グラフィックスパイプラインステートのレジストリ
        std::unordered_map<Core::ResourceNameId, pipelineStateHandle> m_pipelineNameMap; // 名前からグラフィックスパイプラインステートハンドルへのマッピング
        Core::Registry<PipelineTag, DX12ComputePipelineRecord> m_computePipelineRegistry; // コンピュートパイプラインステートのレジストリ
        std::unordered_map<Core::ResourceNameId, pipelineStateHandle> m_computePipelineNameMap; // 名前からコンピュートパイプラインステートハンドルへのマッピング
        Core::Registry<RootSignatureTag, RootSignatureRecord> m_rootSignatureRegistry; // ルートシグネチャのレジストリ
        std::unordered_map<Core::ResourceNameId, rootSignatureHandle> m_rootSignatureNameMap; // 名前からルートシグネチャハンドルへのマッピング
        Core::Registry<ShaderBlobTag, ShaderBlobRecord> m_shaderBlobRegistry; // シェーダーブロブのレジストリ
        std::unordered_map<Core::ResourceNameId, shaderBlobHandle> m_shaderBlobNameMap; // 名前からシェーダーブロブハンドルへのマッピング
    };
}
