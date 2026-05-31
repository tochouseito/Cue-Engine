#pragma once

/// ************************************************************************************
/// D3D12 パイプラインマネージャー
/// ************************************************************************************

// === RHI Includes ===
#include <PipelineManager.h>

// === D3D12 includes ===
#include "DX12Common.h"
#include "HLSLCompiler.h"
#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    struct DX12GraphicsPipelineRecord final
    {
        GraphicsPipelineStateDesc desc; // グラフィックスパイプラインステートの記述
        ComPtr<ID3D12PipelineState> pipelineState; // グラフィックスパイプラインステートオブジェクトの実体
    };

    struct DX12ComputePipelineRecord final
    {
        ComputePipelineStateDesc desc; // コンピュートパイプラインステートの記述
        ComPtr<ID3D12PipelineState> pipelineState; // コンピュートパイプラインステートオブジェクトの実体
    };

    struct RootSignatureRecord final
    {
        RootSignatureDesc desc; // ルートシグネチャの記述
        ComPtr<ID3D12RootSignature> rootSignature; // ルートシグネチャの実体
    };

    struct ShaderBlobRecord final
    {
        ShaderCompileDesc desc; // シェーダーブロブの記述
        ComPtr<IDxcBlob> shaderBlob; // シェーダーブロブの実体
    };

    class DX12PipelineManager final : public IPipelineManager
    {
    public:
        DX12PipelineManager(DX12RenderDevice& renderDevice, HLSLCompiler& hlslCompiler) : m_renderDevice(renderDevice), m_hlslCompiler(hlslCompiler) {}
        ~DX12PipelineManager() override = default;
        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out) override;
        Result destroy_graphics_pipeline(PipelineStateHandle handle) override;
        Result get_graphics_pipeline(std::string_view name, PipelineStateHandle& out) override;
        Result create_compute_pipeline(const ComputePipelineStateDesc& desc, PipelineStateHandle& out) override;
        Result destroy_compute_pipeline(PipelineStateHandle handle) override;
        Result get_compute_pipeline(std::string_view name, PipelineStateHandle& out) override;
        Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out) override;
        Result destroy_root_signature(RootSignatureHandle handle) override;
        Result get_root_signature(std::string_view name, RootSignatureHandle& out) override;
        Result create_shader_blob(const ShaderCompileDesc& desc, ShaderBlobHandle& out) override;
        Result destroy_shader_blob(ShaderBlobHandle handle) override;
        Result get_shader_blob(std::string_view name, ShaderBlobHandle& out) override;
        bool try_get_graphics_pipeline(PipelineStateHandle handle, DX12GraphicsPipelineRecord** outRecord);
        bool try_get_compute_pipeline(PipelineStateHandle handle, DX12ComputePipelineRecord** outRecord);
        bool try_get_root_signature(RootSignatureHandle handle, RootSignatureRecord** outRecord);
        bool try_get_shader_blob(ShaderBlobHandle handle, ShaderBlobRecord** outRecord);
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        HLSLCompiler& m_hlslCompiler;
        Core::Registry<PipelineTag, DX12GraphicsPipelineRecord> m_pipelineRegistry; // グラフィックスパイプラインステートのレジストリ
        std::unordered_map<Core::ResourceNameId, PipelineStateHandle> m_pipelineNameMap; // 名前からグラフィックスパイプラインステートハンドルへのマッピング
        Core::Registry<PipelineTag, DX12ComputePipelineRecord> m_computePipelineRegistry; // コンピュートパイプラインステートのレジストリ
        std::unordered_map<Core::ResourceNameId, PipelineStateHandle> m_computePipelineNameMap; // 名前からコンピュートパイプラインステートハンドルへのマッピング
        Core::Registry<RootSignatureTag, RootSignatureRecord> m_rootSignatureRegistry; // ルートシグネチャのレジストリ
        std::unordered_map<Core::ResourceNameId, RootSignatureHandle> m_rootSignatureNameMap; // 名前からルートシグネチャハンドルへのマッピング
        Core::Registry<ShaderBlobTag, ShaderBlobRecord> m_shaderBlobRegistry; // シェーダーブロブのレジストリ
        std::unordered_map<Core::ResourceNameId, ShaderBlobHandle> m_shaderBlobNameMap; // 名前からシェーダーブロブハンドルへのマッピング
    };
}
