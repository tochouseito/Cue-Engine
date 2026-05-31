#include "DX12PipelineManager.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        D3D12_INPUT_ELEMENT_DESC convert_input_element_desc(const InputElementDesc& desc)
        {
            D3D12_INPUT_ELEMENT_DESC d3dDesc{};
            d3dDesc.SemanticName = desc.semanticName.c_str();
            d3dDesc.SemanticIndex = desc.semanticIndex;
            switch (desc.format)
            {
            case InputElementFormat::R32G32B32A32_Float:
                d3dDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            case InputElementFormat::R32G32B32A32_UInt:
                d3dDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
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
            d3dDesc.InputSlot = desc.inputSlot;
            d3dDesc.AlignedByteOffset = desc.alignedByteOffset;
            d3dDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            d3dDesc.InstanceDataStepRate = 0;
            return d3dDesc;
        }

        D3D12_BLEND_DESC convert_blend_mode(const std::vector<BlendMode>& modes)
        {
            D3D12_BLEND_DESC desc{};
            for (auto& rtDesc : desc.RenderTarget)
            {
                // 色書き込みマスクを既定で有効化し、BlendMode::None でも出力が消えないようにする
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

        D3D12_RASTERIZER_DESC convert_rasterizer_state(const RasterizerStateDesc& desc)
        {
            D3D12_RASTERIZER_DESC d3dDesc{};
            d3dDesc.DepthBias = desc.depthBias;
            d3dDesc.DepthBiasClamp = desc.depthBiasClamp;
            d3dDesc.SlopeScaledDepthBias = desc.slopeScaledDepthBias;
            d3dDesc.DepthClipEnable = TRUE;
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

        D3D12_DEPTH_STENCIL_DESC convert_depth_stencil_state(const DepthStencilStateDesc& desc)
        {
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

        D3D12_DESCRIPTOR_RANGE_TYPE convert_descriptor_range_type(RootParameterType type)
        {
            switch (type)
            {
            case RootParameterType::DescriptorTableCBV:
                return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            case RootParameterType::DescriptorTableSRV:
                return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            case RootParameterType::DescriptorTableUAV:
                return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            default:
                return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            }
        }

        [[nodiscard]] bool is_descriptor_table_parameter(RootParameterType type) noexcept
        {
            switch (type)
            {
            case RootParameterType::DescriptorTableCBV:
            case RootParameterType::DescriptorTableSRV:
            case RootParameterType::DescriptorTableUAV:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] D3D12_ROOT_PARAMETER_TYPE convert_root_parameter_type(RootParameterType type) noexcept
        {
            switch (type)
            {
            case RootParameterType::CBV:
                return D3D12_ROOT_PARAMETER_TYPE_CBV;
            case RootParameterType::SRV:
                return D3D12_ROOT_PARAMETER_TYPE_SRV;
            case RootParameterType::UAV:
                return D3D12_ROOT_PARAMETER_TYPE_UAV;
            default:
                return D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            }
        }

        D3D12_SHADER_VISIBILITY convert_shader_visibility(ShaderVisibility visibility)
        {
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

    Result DX12PipelineManager::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out)
    {
        DX12GraphicsPipelineRecord record{};

        // 入力レイアウトをD3D12_INPUT_ELEMENT_DESCに変換する
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
        for (const auto& elem : desc.inputElements)
        {
            inputElementDescs.push_back(convert_input_element_desc(elem));
        }
        D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
        inputLayoutDesc.pInputElementDescs = inputElementDescs.data();
        inputLayoutDesc.NumElements = static_cast<UINT>(inputElementDescs.size());

        // ブレンドステート、ラスタライザーステート、デプスステンシルステートをD3D12の構造体に変換する
        D3D12_BLEND_DESC blendDesc = convert_blend_mode(desc.blendMode);
        D3D12_RASTERIZER_DESC rasterizerDesc = convert_rasterizer_state(desc.rasterizerState);
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc = convert_depth_stencil_state(desc.depthStencilState);

        // ルートシグネチャをRegistryから取得する
        RootSignatureRecord* rootSignatureRecord = m_rootSignatureRegistry.ref_get(desc.rootSignatureHandle);
        if (!rootSignatureRecord)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Root signature not found for the given handle.");
        }

        // シェーダーブロブをRegistryから取得する
        ShaderBlobRecord* vsBlobRecord = m_shaderBlobRegistry.ref_get(desc.vsHandle);
        if (!vsBlobRecord)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Vertex shader blob not found for the given handle.");
        }
        ShaderBlobRecord* psBlobRecord = m_shaderBlobRegistry.ref_get(desc.psHandle);
        if (!psBlobRecord)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Pixel shader blob not found for the given handle.");
        }

        // D3D12_GRAPHICS_PIPELINE_STATE_DESC を構築する
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignatureRecord->rootSignature.Get();
        psoDesc.VS.pShaderBytecode = vsBlobRecord->shaderBlob->GetBufferPointer();
        psoDesc.VS.BytecodeLength = vsBlobRecord->shaderBlob->GetBufferSize();
        psoDesc.PS.pShaderBytecode = psBlobRecord->shaderBlob->GetBufferPointer();
        psoDesc.PS.BytecodeLength = psBlobRecord->shaderBlob->GetBufferSize();
        psoDesc.InputLayout = inputLayoutDesc;
        psoDesc.BlendState = blendDesc;
        psoDesc.RasterizerState = rasterizerDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.DSVFormat = convert_color_format(desc.dsvFormat);
        psoDesc.PrimitiveTopologyType = convert_primitive_topology_type(desc.primitiveTopologyType);
        psoDesc.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
        for (size_t i = 0; i < desc.rtvFormats.size() && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            psoDesc.RTVFormats[i] = convert_color_format(desc.rtvFormats[i]);
        }
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

        // ID3D12PipelineState を作成する
        ComPtr<ID3D12PipelineState> pipelineState;
        HRESULT hr = m_renderDevice.get_d3d12_device()->CreateGraphicsPipelineState(
            &psoDesc,
            IID_PPV_ARGS(&pipelineState)
        );
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create graphics pipeline state.");
        }

        // 作成したパイプラインステートをRegistryに登録する
        record.pipelineState = pipelineState;
        record.desc = desc;
        PipelineStateHandle handle = m_pipelineRegistry.create(record);

        // 名前が指定されていれば名前マップに登録する
        if (!desc.name.empty())
        {
            m_pipelineNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        // 成功結果を返す
        out = handle;
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_graphics_pipeline(PipelineStateHandle handle)
    {
        // ハンドルが有効かをチェックする
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid pipeline state handle.");
        }

        // 名前マップから削除する
        for (auto it = m_pipelineNameMap.begin(); it != m_pipelineNameMap.end(); ++it)
        {
            if (it->second == handle)
            {
                m_pipelineNameMap.erase(it);
                break;
            }
        }

        // レジストリから削除する
        if (!m_pipelineRegistry.destroy(handle))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Pipeline state not found for the given handle.");
        }
        return Result::ok();
    }
    Result DX12PipelineManager::get_graphics_pipeline(std::string_view name, PipelineStateHandle& out)
    {
        if (!m_pipelineNameMap.contains(Core::fnv1a64(name)))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Pipeline state not found for the given name.");
        }
        out = m_pipelineNameMap[Core::fnv1a64(name)];
        return Result::ok();
    }
    Result DX12PipelineManager::create_compute_pipeline(const ComputePipelineStateDesc& desc, PipelineStateHandle& out)
    {
        DX12ComputePipelineRecord record{};

        RootSignatureRecord* rootSignatureRecord = m_rootSignatureRegistry.ref_get(desc.rootSignatureHandle);
        if (!rootSignatureRecord)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Root signature not found for the given compute pipeline.");
        }

        ShaderBlobRecord* csBlobRecord = m_shaderBlobRegistry.ref_get(desc.csHandle);
        if (!csBlobRecord)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Compute shader blob not found for the given handle.");
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignatureRecord->rootSignature.Get();
        psoDesc.CS.pShaderBytecode = csBlobRecord->shaderBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlobRecord->shaderBlob->GetBufferSize();

        ComPtr<ID3D12PipelineState> pipelineState;
        HRESULT hr = m_renderDevice.get_d3d12_device()->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(&pipelineState));
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create compute pipeline state.");
        }

        record.pipelineState = pipelineState;
        record.desc = desc;
        PipelineStateHandle handle = m_computePipelineRegistry.create(record);
        if (!desc.name.empty())
        {
            m_computePipelineNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = handle;
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_compute_pipeline(PipelineStateHandle handle)
    {
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid compute pipeline state handle.");
        }

        for (auto it = m_computePipelineNameMap.begin(); it != m_computePipelineNameMap.end(); ++it)
        {
            if (it->second == handle)
            {
                m_computePipelineNameMap.erase(it);
                break;
            }
        }

        if (!m_computePipelineRegistry.destroy(handle))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Compute pipeline state not found for the given handle.");
        }

        return Result::ok();
    }
    Result DX12PipelineManager::get_compute_pipeline(std::string_view name, PipelineStateHandle& out)
    {
        if (!m_computePipelineNameMap.contains(Core::fnv1a64(name)))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Compute pipeline state not found for the given name.");
        }

        out = m_computePipelineNameMap[Core::fnv1a64(name)];
        return Result::ok();
    }
    Result DX12PipelineManager::create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out)
    {
        // D3D12_ROOT_SIGNATURE_DESC を構築する
        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        // RootParameterDesc を descriptor table / constants に変換し、FrameGraph の descriptor 解決結果をそのまま bind できる形にする
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
                d3dParam.Constants.RegisterSpace = parmDesc.registerSpace;
                d3dParam.Constants.Num32BitValues = 1;
                d3dParameters.push_back(d3dParam);
                continue;
            }

            if (is_descriptor_table_parameter(parmDesc.type))
            {
                D3D12_DESCRIPTOR_RANGE descriptorRange{};
                descriptorRange.RangeType = convert_descriptor_range_type(parmDesc.type);
                descriptorRange.NumDescriptors =
                    parmDesc.descriptorCount == 0
                    ? UINT_MAX
                    : parmDesc.descriptorCount;
                descriptorRange.BaseShaderRegister = parmDesc.shaderRegister;
                descriptorRange.RegisterSpace = parmDesc.registerSpace;
                descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                d3dDescriptorRanges.push_back(descriptorRange);

                d3dParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                d3dParam.DescriptorTable.NumDescriptorRanges = 1;
                d3dParam.DescriptorTable.pDescriptorRanges = &d3dDescriptorRanges.back();
                d3dParameters.push_back(d3dParam);
                continue;
            }

            d3dParam.ParameterType = convert_root_parameter_type(parmDesc.type);
            d3dParam.Descriptor.ShaderRegister = parmDesc.shaderRegister;
            d3dParam.Descriptor.RegisterSpace = parmDesc.registerSpace;
            d3dParameters.push_back(d3dParam);
        }

        // D3D12_ROOT_SIGNATURE_DESC にパラメータをセットする
        rootSignatureDesc.NumParameters = static_cast<UINT>(d3dParameters.size());
        rootSignatureDesc.pParameters = d3dParameters.data();

        D3D12_STATIC_SAMPLER_DESC staticSampler{};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.MipLODBias = 0.0f;
        staticSampler.MaxAnisotropy = 1;
        staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSampler.MinLOD = 0.0f;
        staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
        staticSampler.ShaderRegister = 0;
        staticSampler.RegisterSpace = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootSignatureDesc.NumStaticSamplers = 1;
        rootSignatureDesc.pStaticSamplers = &staticSampler;

        // D3D12_ROOT_SIGNATURE_DESC をシリアライズしてバイナリ化する
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
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to serialize root signature");
        }
        RootSignatureRecord rootSigRecord{};
        hr = m_renderDevice.get_d3d12_device()->CreateRootSignature(
            0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&rootSigRecord.rootSignature));
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create root signature from serialized blob");
        }

        rootSigRecord.desc = desc;

        // 作成したルートシグネチャをRegistryに登録する
        RootSignatureHandle handle = m_rootSignatureRegistry.create(rootSigRecord);

        // 名前が指定されていれば名前マップに登録する
        if (!desc.name.empty())
        {
            m_rootSignatureNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        // 成功結果を返す
        out = handle;
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_root_signature(RootSignatureHandle handle)
    {
        // ハンドルが有効かをチェックする
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid root signature handle.");
        }

        // 名前マップから削除する
        for (auto it = m_rootSignatureNameMap.begin(); it != m_rootSignatureNameMap.end(); ++it)
        {
            if (it->second == handle)
            {
                m_rootSignatureNameMap.erase(it);
                break;
            }
        }

        // レジストリから削除する
        if (!m_rootSignatureRegistry.destroy(handle))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Root signature not found for the given handle.");
        }
        return Result::ok();
    }
    Result DX12PipelineManager::get_root_signature(std::string_view name, RootSignatureHandle& out)
    {
        if (!m_rootSignatureNameMap.contains(Core::fnv1a64(name)))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Root signature not found for the given name.");
        }
        out = m_rootSignatureNameMap[Core::fnv1a64(name)];
        return Result::ok();
    }
    Result DX12PipelineManager::create_shader_blob(const ShaderCompileDesc& desc, ShaderBlobHandle& out)
    {
        // HLSLCompiler の失敗を Result で受け、失敗時に無効ハンドルを成功扱いしない
        ShaderBlobRecord blobRecord{};
        Result result = m_hlslCompiler.compile_shader_raw(desc, &blobRecord.shaderBlob);
        if (!result)
        {
            return result;
        }

        // コンパイル成功時だけレジストリと名前引きを更新する
        ShaderBlobHandle handle = m_shaderBlobRegistry.create(blobRecord);

        // 名前が指定されていれば名前マップに登録する
        if (!desc.name.empty())
        {
            m_shaderBlobNameMap[Core::fnv1a64(desc.name)] = handle;
        }

        // 成功結果を返す
        out = std::move(handle);
        return Result::ok();
    }
    Result DX12PipelineManager::destroy_shader_blob(ShaderBlobHandle handle)
    {
        // ハンドルが有効かをチェックする
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid shader blob handle.");
        }

        // 名前マップから削除する
        for (auto it = m_shaderBlobNameMap.begin(); it != m_shaderBlobNameMap.end(); ++it)
        {
            if (it->second == handle)
            {
                m_shaderBlobNameMap.erase(it);
                break;
            }
        }

        // レジストリから削除する
        if (!m_shaderBlobRegistry.destroy(handle))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Shader blob not found for the given handle.");
        }

        return Result::ok();
    }
    Result DX12PipelineManager::get_shader_blob(std::string_view name, ShaderBlobHandle& out)
    {
        if (m_shaderBlobNameMap.contains(Core::fnv1a64(name)))
        {
            out = m_shaderBlobNameMap[Core::fnv1a64(name)];
            return Result::ok();
        }
        else
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer with the given name was not found.");
        }
    }
    bool DX12PipelineManager::try_get_graphics_pipeline(PipelineStateHandle handle, DX12GraphicsPipelineRecord** outRecord)
    {
        // ハンドルの解決とレコードの取得
        *outRecord = nullptr;
        *outRecord = m_pipelineRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }
    bool DX12PipelineManager::try_get_compute_pipeline(PipelineStateHandle handle, DX12ComputePipelineRecord** outRecord)
    {
        *outRecord = nullptr;
        *outRecord = m_computePipelineRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }
    bool DX12PipelineManager::try_get_root_signature(RootSignatureHandle handle, RootSignatureRecord** outRecord)
    {
        // ハンドルの解決とルートシグネチャの取得
        *outRecord = nullptr;
        *outRecord = m_rootSignatureRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }
    bool DX12PipelineManager::try_get_shader_blob(ShaderBlobHandle handle, ShaderBlobRecord** outRecord)
    {
        // ハンドルの解決とシェーダーブロブの取得
        *outRecord = nullptr;
        *outRecord = m_shaderBlobRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }
}
