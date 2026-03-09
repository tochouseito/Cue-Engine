#include "DX12PipelineManager.h"

namespace
{
    D3D12_INPUT_ELEMENT_DESC convert_input_element_desc(const Cue::GraphicsCore::InputElementDesc& desc)
    {
        using namespace Cue::GraphicsCore;
        D3D12_INPUT_ELEMENT_DESC d3dDesc{};
        d3dDesc.SemanticName = desc.semanticName.c_str();
        d3dDesc.SemanticIndex = desc.semanticIndex;
        switch (desc.format)
        {
        case InputElementFormat::R32G32B32A32_Float:
            d3dDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case InputElementFormat::R32G32B32_Float:
            d3dDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
            break;
        case InputElementFormat::R32G32_Float:
            d3dDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
            break;
        case InputElementFormat::R32_Float:
            d3dDesc.Format = DXGI_FORMAT_R32_FLOAT;
            break;
        default:
            d3dDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        }
        d3dDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        return d3dDesc;
    }

    D3D12_BLEND_DESC convert_blend_mode(const std::vector<Cue::GraphicsCore::BlendMode>& modes)
    {
        using namespace Cue::GraphicsCore;
        D3D12_BLEND_DESC desc{};
        for (auto& rtDesc : desc.RenderTarget)
        {
            // 1) 色書き込みマスクを既定で有効化し、BlendMode::None でも出力が消えないようにする。
            rtDesc.BlendEnable = FALSE;
            rtDesc.LogicOpEnable = FALSE;
            rtDesc.SrcBlend = D3D12_BLEND_ONE;
            rtDesc.DestBlend = D3D12_BLEND_ZERO;
            rtDesc.BlendOp = D3D12_BLEND_OP_ADD;
            rtDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
            rtDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
            rtDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rtDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
            rtDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        for (size_t i = 0; i < modes.size() && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            D3D12_RENDER_TARGET_BLEND_DESC& rtDesc = desc.RenderTarget[i];
            switch (modes[i])
            {
            case BlendMode::None:
                rtDesc.BlendEnable = FALSE;
                break;
            case BlendMode::Normal:
                rtDesc.BlendEnable = TRUE;
                rtDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rtDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rtDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
                rtDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                break;
            default:
                rtDesc.BlendEnable = FALSE;
                break;
            }
        }
        return desc;
    }

    D3D12_RASTERIZER_DESC convert_rasterizer_state(const Cue::GraphicsCore::RasterizerStateDesc& desc)
    {
        using namespace Cue::GraphicsCore;
        D3D12_RASTERIZER_DESC d3dDesc{};
        switch (desc.cullMode)
        {
        case CullMode::None:
            d3dDesc.CullMode = D3D12_CULL_MODE_NONE;
            break;
        case CullMode::Front:
            d3dDesc.CullMode = D3D12_CULL_MODE_FRONT;
            break;
        case CullMode::Back:
            d3dDesc.CullMode = D3D12_CULL_MODE_BACK;
            break;
        default:
            d3dDesc.CullMode = D3D12_CULL_MODE_BACK;
            break;
        }
        switch (desc.fillMode)
        {
        case FillMode::Solid:
            d3dDesc.FillMode = D3D12_FILL_MODE_SOLID;
            break;
        case FillMode::Wireframe:
            d3dDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
            break;
        default:
            d3dDesc.FillMode = D3D12_FILL_MODE_SOLID;
            break;
        }
        return d3dDesc;
    }

    D3D12_DEPTH_STENCIL_DESC convert_depth_stencil_state(const Cue::GraphicsCore::DepthStencilStateDesc& desc)
    {
        using namespace Cue::GraphicsCore;
        D3D12_DEPTH_STENCIL_DESC d3dDesc{};
        d3dDesc.DepthEnable = desc.depthEnable ? TRUE : FALSE;
        d3dDesc.DepthWriteMask = (desc.depthWriteMask == DepthWriteMask::All) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        switch (desc.depthFunc)
        {
        case ComparisonFunc::LessEqual:
            d3dDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            break;
        default:
            d3dDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            break;
        }
        return d3dDesc;
    }

    DXGI_FORMAT convert_dsv_format(Cue::GraphicsCore::DSVFormat format)
    {
        using namespace Cue::GraphicsCore;
        switch (format)
        {
        case DSVFormat::D24_UNorm_S8_UInt:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        }
    }

    D3D12_PRIMITIVE_TOPOLOGY_TYPE convert_primitive_topology_type(Cue::GraphicsCore::PrimitiveTopologyType type)
    {
        using namespace Cue::GraphicsCore;
        switch (type)
        {
        case PrimitiveTopologyType::Triangle:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        default:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        }
    }

    D3D12_DESCRIPTOR_RANGE_TYPE convert_descriptor_range_type(Cue::GraphicsCore::RootParameterType type)
    {
        using namespace Cue::GraphicsCore;
        switch (type)
        {
        case RootParameterType::CBV:
            return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        case RootParameterType::SRV:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RootParameterType::UAV:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        default:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        }
    }

    D3D12_SHADER_VISIBILITY convert_shader_visibility(Cue::GraphicsCore::ShaderVisibility visibility)
    {
        using namespace Cue::GraphicsCore;
        switch (visibility)
        {
        case ShaderVisibility::All:
            return D3D12_SHADER_VISIBILITY_ALL;
        case ShaderVisibility::Vertex:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case ShaderVisibility::Pixel:
            return D3D12_SHADER_VISIBILITY_PIXEL;
        default:
            return D3D12_SHADER_VISIBILITY_ALL;
        }
    }
}

namespace Cue::GraphicsCore::DX12
{
    Result DX12PipelineManager::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& outHandle)
    {
        // 1) 入力レイアウトをD3D12_INPUT_ELEMENT_DESCに変換する。
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
        for (const auto& elem : desc.inputElements)
        {
            inputElementDescs.push_back(convert_input_element_desc(elem));
        }
        D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
        inputLayoutDesc.pInputElementDescs = inputElementDescs.data();
        inputLayoutDesc.NumElements = static_cast<UINT>(inputElementDescs.size());

        // 2) ブレンドステート、ラスタライザーステート、デプスステンシルステートをD3D12の構造体に変換する。
        D3D12_BLEND_DESC blendDesc = convert_blend_mode(desc.blendMode);
        D3D12_RASTERIZER_DESC rasterizerDesc = convert_rasterizer_state(desc.rasterizerState);
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc = convert_depth_stencil_state(desc.depthStencilState);

        // 3) ルートシグネチャをRegistryから取得する。
        ComPtr<ID3D12RootSignature> rootSignature;
        bool result = m_rootSignatureRegistry.try_get(desc.rootSignatureHandle, rootSignature);
        if (!result)
        {
            return Result::fail(
                Facility::Graphics,
                Code::NotFound,
                Severity::Error,
                0,
                "Root signature not found for the given handle."
            );
        }

        // 4) シェーダーブロブをRegistryから取得する。
        ComPtr<IDxcBlob> vsBlob;
        result = m_shaderBlobRegistry.try_get(desc.vsHandle, vsBlob);
        if (!result)
        {
            return Result::fail(
                Facility::Graphics,
                Code::NotFound,
                Severity::Error,
                0,
                "Vertex shader blob not found for the given handle."
            );
        }
        ComPtr<IDxcBlob> psBlob;
        result = m_shaderBlobRegistry.try_get(desc.psHandle, psBlob);
        if (!result)
        {
            return Result::fail(
                Facility::Graphics,
                Code::NotFound,
                Severity::Error,
                0,
                "Pixel shader blob not found for the given handle."
            );
        }

        // 5) D3D12_GRAPHICS_PIPELINE_STATE_DESC を構築する。
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature.Get();
        psoDesc.VS.pShaderBytecode = vsBlob->GetBufferPointer();
        psoDesc.VS.BytecodeLength = vsBlob->GetBufferSize();
        psoDesc.PS.pShaderBytecode = psBlob->GetBufferPointer();
        psoDesc.PS.BytecodeLength = psBlob->GetBufferSize();
        psoDesc.InputLayout = inputLayoutDesc;
        psoDesc.BlendState = blendDesc;
        psoDesc.RasterizerState = rasterizerDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.DSVFormat = convert_dsv_format(desc.dsvFormat);
        psoDesc.PrimitiveTopologyType = convert_primitive_topology_type(desc.primitiveTopologyType);
        psoDesc.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
        for (size_t i = 0; i < desc.rtvFormats.size() && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            psoDesc.RTVFormats[i] = convert_color_format(desc.rtvFormats[i]);
        }
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

        // 6) ID3D12PipelineState を作成する。
        ComPtr<ID3D12PipelineState> pipelineState;
        HRESULT hr = m_renderDevice.get_d3d12_device()->CreateGraphicsPipelineState(
            &psoDesc,
            IID_PPV_ARGS(&pipelineState)
        );
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::D3D12,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create graphics pipeline state object."
            );
        }

        // 7) 作成したパイプラインステートをRegistryに登録する。
        PipelineStateHandle handle = m_pipelineRegistry.create(pipelineState);

        // 8) 名前が指定されていれば名前マップに登録する。
        if (!desc.name.empty())
        {
            m_pipelineNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        // 9) 成功結果を返す。
        outHandle = handle;
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_pipeline(const PipelineStateHandle& handle)
    {
        if (!m_pipelineRegistry.destroy(handle))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Pipeline handle is not alive");
        }
        std::erase_if(m_pipelineNameMap, [&handle](const auto& pair) { return pair.second == handle; });

        return Result::ok();
    }
    Result DX12PipelineManager::get_pipeline(ResourceNameId nameId, PipelineStateHandle& outHandle)
    {
        if (m_pipelineNameMap.contains(nameId))
        {
            outHandle = m_pipelineNameMap[nameId];
            return Result::ok();
        }
        return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Pipeline not found");
    }
    Result DX12PipelineManager::create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& outHandle)
    {
        // 1) D3D12_ROOT_SIGNATURE_DESC を構築する。
        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        // 2) RootParameterDesc を descriptor table / constants に変換し、FrameGraph の descriptor 解決結果をそのまま bind できる形にする。
        std::vector<D3D12_ROOT_PARAMETER> d3dParameters;
        std::vector<D3D12_DESCRIPTOR_RANGE> d3dDescriptorRanges;
        d3dParameters.reserve(desc.parameters.size());
        d3dDescriptorRanges.reserve(desc.parameters.size());
        for (const RootParameterDesc& parmDesc : desc.parameters)
        {
            D3D12_ROOT_PARAMETER d3dParam{};
            d3dParam.ShaderVisibility = convert_shader_visibility(parmDesc.visibility);

            if (parmDesc.type == RootParameterType::_32BitConstants)
            {
                d3dParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                d3dParam.Constants.ShaderRegister = parmDesc.shaderRegister;
                d3dParam.Constants.RegisterSpace = 0;
                d3dParam.Constants.Num32BitValues = 1;
                d3dParameters.push_back(d3dParam);
                continue;
            }

            D3D12_DESCRIPTOR_RANGE descriptorRange{};
            descriptorRange.RangeType = convert_descriptor_range_type(parmDesc.type);
            descriptorRange.NumDescriptors = 1;
            descriptorRange.BaseShaderRegister = parmDesc.shaderRegister;
            descriptorRange.RegisterSpace = 0;
            descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            d3dDescriptorRanges.push_back(descriptorRange);

            d3dParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            d3dParam.DescriptorTable.NumDescriptorRanges = 1;
            d3dParam.DescriptorTable.pDescriptorRanges = &d3dDescriptorRanges.back();
            d3dParameters.push_back(d3dParam);
        }

        // 3) D3D12_ROOT_SIGNATURE_DESC にパラメータをセットする。
        rootSignatureDesc.NumParameters = static_cast<UINT>(d3dParameters.size());
        rootSignatureDesc.pParameters = d3dParameters.data();

        // 4) D3D12_ROOT_SIGNATURE_DESC をシリアライズしてバイナリ化する。
        ComPtr<ID3DBlob> serializedRootSig;
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSig,
            &errorBlob);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to serialize root signature.");
        }
        ComPtr<ID3D12RootSignature> rootSignature;
        hr = m_renderDevice.get_d3d12_device()->CreateRootSignature(
            0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create root signature.");
        }

        // 5) 作成したルートシグネチャをRegistryに登録する。
        RootSignatureHandle handle = m_rootSignatureRegistry.create(rootSignature);

        // 6) 名前が指定されていれば名前マップに登録する。
        if (!desc.name.empty())
        {
            m_rootSignatureNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        // 7) 成功結果を返す。
        outHandle = handle;
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_root_signature(const RootSignatureHandle& handle)
    {
        if (!m_rootSignatureRegistry.destroy(handle))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Root signature handle is not alive");
        }
        std::erase_if(m_rootSignatureNameMap, [&handle](const auto& pair) { return pair.second == handle; });

        return Result::ok();
    }
    Result DX12PipelineManager::get_root_signature(ResourceNameId nameId, RootSignatureHandle& outHandle)
    {
        if (m_rootSignatureNameMap.contains(nameId))
        {
            outHandle = m_rootSignatureNameMap[nameId];
            return Result::ok();
        }
        return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Root signature not found");
    }
    Result DX12PipelineManager::compile_shader(const ShaderCompileDesc& desc, ShaderBlobHandle& outHandle)
    {
        // 1) HLSLCompilerを使ってシェーダーをコンパイルする。
        ComPtr<IDxcBlob> blob = m_shaderCompiler.compile_shader_raw(desc);

        // 2) コンパイル結果をShaderBlobRegistryに登録する。
        ShaderBlobHandle handle = m_shaderBlobRegistry.create(blob);

        // 3) 名前が指定されていれば名前マップに登録する。
        if (!desc.name.empty())
        {
            m_shaderBlobNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        // 4) 成功結果を返す。
        outHandle = handle;
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_shader(const ShaderBlobHandle& handle)
    {
        if (!m_shaderBlobRegistry.destroy(handle))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Shader handle is not alive");
        }
        std::erase_if(m_shaderBlobNameMap, [&handle](const auto& pair) { return pair.second == handle; });

        return Result::ok();
    }
    Result DX12PipelineManager::get_shader(ResourceNameId nameId, ShaderBlobHandle& outHandle)
    {
        if (m_shaderBlobNameMap.contains(nameId))
        {
            outHandle = m_shaderBlobNameMap[nameId];
            return Result::ok();
        }
        return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Shader not found");
    }
    Result DX12PipelineManager::resolve_pipeline_state(PipelineStateHandle pipelineHandle, ID3D12PipelineState*& outPipelineState) const
    {
        // 1) 抽象 pipeline handle から DX12 実体を逆引きし、command context での bind 前に存在を検証する。
        ComPtr<ID3D12PipelineState> pipelineState = nullptr;
        if (!m_pipelineRegistry.try_get(pipelineHandle, pipelineState))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Error, 0, "Pipeline state handle is not alive.");
        }

        outPipelineState = pipelineState.Get();
        return Result::ok();
    }
    Result DX12PipelineManager::resolve_root_signature(RootSignatureHandle rootSignatureHandle, ID3D12RootSignature*& outRootSignature) const
    {
        // 1) 抽象 root signature handle から DX12 実体を逆引きし、command context の bind 先を確定する。
        ComPtr<ID3D12RootSignature> rootSignature = nullptr;
        if (!m_rootSignatureRegistry.try_get(rootSignatureHandle, rootSignature))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Error, 0, "Root signature handle is not alive.");
        }

        outRootSignature = rootSignature.Get();
        return Result::ok();
    }
} // namespace Cue::GraphicsCore::DX12
