#include "DX12PipelineManager.h"

#include <IO/Logger.h>

#include <cstddef>
#include <vector>

namespace Cue::RHI::DX12
{
    namespace
    {
        std::string g_lastRootSignatureSerializeError{};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
        template <typename T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type>
        struct alignas(void*) PipelineStreamSubobject final
        {
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
            T value{};
        };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

        struct MeshPipelineStream final
        {
            PipelineStreamSubobject<ID3D12RootSignature*, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> rootSignature;
            PipelineStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> ms;
            PipelineStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> ps;
            PipelineStreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
            PipelineStreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> rasterizer;
            PipelineStreamSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> depthStencil;
            PipelineStreamSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY> primitiveTopology;
            PipelineStreamSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> rtvFormats;
            PipelineStreamSubobject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> dsvFormat;
            PipelineStreamSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sampleDesc;
            PipelineStreamSubobject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> sampleMask;
        };

        struct MeshPipelineStreamWithAs final
        {
            PipelineStreamSubobject<ID3D12RootSignature*, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> rootSignature;
            PipelineStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> as;
            PipelineStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> ms;
            PipelineStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> ps;
            PipelineStreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
            PipelineStreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> rasterizer;
            PipelineStreamSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> depthStencil;
            PipelineStreamSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY> primitiveTopology;
            PipelineStreamSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> rtvFormats;
            PipelineStreamSubobject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> dsvFormat;
            PipelineStreamSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sampleDesc;
            PipelineStreamSubobject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> sampleMask;
        };

        D3D12_SHADER_BYTECODE shader_bytecode(const ShaderBlobRecord& record)
        {
            D3D12_SHADER_BYTECODE bytecode{};
            bytecode.pShaderBytecode = record.shaderBlob->GetBufferPointer();
            bytecode.BytecodeLength = record.shaderBlob->GetBufferSize();
            return bytecode;
        }

        uint64_t info_queue_message_count(ID3D12Device* device)
        {
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (device == nullptr ||
                FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))) ||
                infoQueue == nullptr)
            {
                return 0;
            }
            return infoQueue->GetNumStoredMessages();
        }

        void log_info_queue_messages(ID3D12Device* device,
                                     uint64_t firstMessageIndex)
        {
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (device == nullptr ||
                FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))) ||
                infoQueue == nullptr)
            {
                Core::IO::log(Core::IO::LogSink::console |
                                  Core::IO::LogSink::file,
                              "[D3D12][InfoQueue] unavailable");
                return;
            }

            const uint64_t messageCount = infoQueue->GetNumStoredMessages();
            for (uint64_t i = firstMessageIndex; i < messageCount; ++i)
            {
                SIZE_T messageLength = 0;
                if (FAILED(infoQueue->GetMessage(i, nullptr, &messageLength)) ||
                    messageLength == 0)
                {
                    continue;
                }

                std::vector<std::byte> storage(messageLength);
                auto* message =
                    reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (FAILED(infoQueue->GetMessage(i, message, &messageLength)) ||
                    message == nullptr)
                {
                    continue;
                }

                Core::IO::log(
                    Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "[D3D12][InfoQueue] category={} severity={} id={} {}",
                    static_cast<uint32_t>(message->Category),
                    static_cast<uint32_t>(message->Severity),
                    static_cast<uint32_t>(message->ID),
                    message->pDescription != nullptr ? message->pDescription
                                                     : "");
            }
        }

        Result get_device2(ID3D12Device* device, ComPtr<ID3D12Device2>& out)
        {
            out.Reset();
            HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&out));
            if (FAILED(hr) || out == nullptr)
            {
                return Result::fail(PAL::Win::convert_hresult_code(hr),
                                    Severity::Error,
                                    "D3D12 device does not support ID3D12Device2.");
            }
            return Result::ok();
        }

        D3D12_INPUT_ELEMENT_DESC convert_input_element_desc(const InputElementDesc& desc)
        {
            // RHI の vertex input 記述を D3D12 の PSO 入力レイアウトへ変換する。
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
            // 先に全 render target を安全な無効 blend 状態で初期化し、
            // 指定された slot だけを上書きする。
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
            // Descriptor table 系 root parameter は range type に落として root signature へ詰める。
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
            case ShaderVisibility::Amplification:
                return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
            case ShaderVisibility::Mesh:
                return D3D12_SHADER_VISIBILITY_MESH;
            default:
                return D3D12_SHADER_VISIBILITY_ALL;
            }
        }
    }

    Result DX12PipelineManager::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out)
    {
        // Graphics PSO は root signature、shader blob、固定機能 state をまとめて不変 object として作る。
        // 作成後は command list 側で handle から取得して SetPipelineState する。
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

        if (desc.msHandle.valid())
        {
            ShaderBlobRecord* msBlobRecord = m_shaderBlobRegistry.ref_get(desc.msHandle);
            if (!msBlobRecord)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Mesh shader blob not found for the given handle.");
            }
            ShaderBlobRecord* psBlobRecord = m_shaderBlobRegistry.ref_get(desc.psHandle);
            if (!psBlobRecord)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Pixel shader blob not found for the given handle.");
            }

            D3D12_RT_FORMAT_ARRAY rtvFormats{};
            rtvFormats.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
            for (size_t i = 0; i < desc.rtvFormats.size() && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            {
                rtvFormats.RTFormats[i] = convert_color_format(desc.rtvFormats[i]);
            }

            ComPtr<ID3D12PipelineState> pipelineState;
            ComPtr<ID3D12Device2> device2;
            Result device2Result =
                get_device2(m_renderDevice.get_d3d12_device(), device2);
            if (!device2Result)
            {
                return device2Result;
            }
            HRESULT hr = S_OK;
            uint64_t infoQueueMessageStart = 0;
            size_t asBlobSize = 0;
            const size_t msBlobSize =
                msBlobRecord->shaderBlob != nullptr
                    ? msBlobRecord->shaderBlob->GetBufferSize()
                    : 0;
            const size_t psBlobSize =
                psBlobRecord->shaderBlob != nullptr
                    ? psBlobRecord->shaderBlob->GetBufferSize()
                    : 0;
            if (desc.asHandle.valid())
            {
                ShaderBlobRecord* asBlobRecord = m_shaderBlobRegistry.ref_get(desc.asHandle);
                if (!asBlobRecord)
                {
                    return Result::fail(
                        Code::NotFound,
                        Severity::Error,
                        "Amplification shader blob not found for the given handle.");
                }
                asBlobSize = asBlobRecord->shaderBlob != nullptr
                                 ? asBlobRecord->shaderBlob->GetBufferSize()
                                 : 0;

                MeshPipelineStreamWithAs stream{};
                stream.rootSignature.value = rootSignatureRecord->rootSignature.Get();
                stream.as.value = shader_bytecode(*asBlobRecord);
                stream.ms.value = shader_bytecode(*msBlobRecord);
                stream.ps.value = shader_bytecode(*psBlobRecord);
                stream.blend.value = blendDesc;
                stream.rasterizer.value = rasterizerDesc;
                stream.depthStencil.value = depthStencilDesc;
                stream.primitiveTopology.value = convert_primitive_topology_type(desc.primitiveTopologyType);
                stream.rtvFormats.value = rtvFormats;
                stream.dsvFormat.value = convert_color_format(desc.dsvFormat);
                stream.sampleDesc.value = DXGI_SAMPLE_DESC{1, 0};
                stream.sampleMask.value = D3D12_DEFAULT_SAMPLE_MASK;
                D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
                streamDesc.SizeInBytes = sizeof(stream);
                streamDesc.pPipelineStateSubobjectStream = &stream;
                infoQueueMessageStart =
                    info_queue_message_count(m_renderDevice.get_d3d12_device());
                Core::IO::log(
                    Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "[D3D12][MeshPSO] create name={} as=true streamBytes={} "
                    "asBytes={} msBytes={} psBytes={} rtvCount={} dsv={}",
                    desc.name, static_cast<uint64_t>(streamDesc.SizeInBytes),
                    static_cast<uint64_t>(asBlobSize),
                    static_cast<uint64_t>(msBlobSize),
                    static_cast<uint64_t>(psBlobSize),
                    static_cast<uint32_t>(rtvFormats.NumRenderTargets),
                    static_cast<uint32_t>(stream.dsvFormat.value));
                Core::IO::log(
                    Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "[D3D12][MeshPSO] streamOffsets root={} as={} ms={} ps={} "
                    "blend={} raster={} depth={} topology={} rtv={} dsv={} "
                    "sampleDesc={} sampleMask={}",
                    static_cast<uint64_t>(offsetof(MeshPipelineStreamWithAs,
                                                   rootSignature)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, as)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, ms)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, ps)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, blend)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, rasterizer)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, depthStencil)),
                    static_cast<uint64_t>(offsetof(
                        MeshPipelineStreamWithAs, primitiveTopology)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, rtvFormats)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, dsvFormat)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, sampleDesc)),
                    static_cast<uint64_t>(
                        offsetof(MeshPipelineStreamWithAs, sampleMask)));
                hr = device2->CreatePipelineState(
                    &streamDesc, IID_PPV_ARGS(&pipelineState));
            }
            else
            {
                MeshPipelineStream stream{};
                stream.rootSignature.value = rootSignatureRecord->rootSignature.Get();
                stream.ms.value = shader_bytecode(*msBlobRecord);
                stream.ps.value = shader_bytecode(*psBlobRecord);
                stream.blend.value = blendDesc;
                stream.rasterizer.value = rasterizerDesc;
                stream.depthStencil.value = depthStencilDesc;
                stream.primitiveTopology.value = convert_primitive_topology_type(desc.primitiveTopologyType);
                stream.rtvFormats.value = rtvFormats;
                stream.dsvFormat.value = convert_color_format(desc.dsvFormat);
                stream.sampleDesc.value = DXGI_SAMPLE_DESC{1, 0};
                stream.sampleMask.value = D3D12_DEFAULT_SAMPLE_MASK;
                D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
                streamDesc.SizeInBytes = sizeof(stream);
                streamDesc.pPipelineStateSubobjectStream = &stream;
                infoQueueMessageStart =
                    info_queue_message_count(m_renderDevice.get_d3d12_device());
                hr = device2->CreatePipelineState(
                    &streamDesc, IID_PPV_ARGS(&pipelineState));
            }

            if (FAILED(hr))
            {
                Core::IO::log(
                    Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "[D3D12][MeshPSO] failed name={} as={} hr=0x{:08X} "
                    "asBytes={} msBytes={} psBytes={}",
                    desc.name, desc.asHandle.valid() ? "true" : "false",
                    static_cast<uint32_t>(hr),
                    static_cast<uint64_t>(asBlobSize),
                    static_cast<uint64_t>(msBlobSize),
                    static_cast<uint64_t>(psBlobSize));
                log_info_queue_messages(m_renderDevice.get_d3d12_device(),
                                        infoQueueMessageStart);
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to create mesh shader graphics pipeline state.");
            }

            record.pipelineState = pipelineState;
            record.desc = desc;
            PipelineStateHandle handle = m_pipelineRegistry.create(record);
            if (!desc.name.empty())
            {
                m_pipelineNameMap[Core::fnv1a64(desc.name)] = handle;
            }
            out = handle;
            return Result::ok();
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
        // RHI の root parameter 記述を D3D12_ROOT_SIGNATURE_DESC へ展開する。
        // descriptor range の配列は serialize 完了まで生存している必要があるため、関数内 vector に保持する。
        // Mesh/Amplification shader から GPU 更新済み SRV/UAV を参照できるよう、
        // Root Signature 1.1 の volatility flags を明示して作成する。
        constexpr D3D12_ROOT_SIGNATURE_FLAGS k_rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        // RootParameterDesc を descriptor table / constants に変換し、FrameGraph の descriptor 解決結果をそのまま bind できる形にする
        std::vector<D3D12_ROOT_PARAMETER1> d3dParameters;
        std::vector<D3D12_DESCRIPTOR_RANGE1> d3dDescriptorRanges;
        d3dParameters.reserve(desc.parameters.size());
        d3dDescriptorRanges.reserve(desc.parameters.size());
        for (const RootParameterDesc& parmDesc : desc.parameters)
        {
            D3D12_ROOT_PARAMETER1 d3dParam{};
            d3dParam.ShaderVisibility = convert_shader_visibility(parmDesc.visibility);

            if (parmDesc.type == RootParameterType::_32BitConstants)
            {
                d3dParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                d3dParam.Constants.ShaderRegister = parmDesc.shaderRegister;
                d3dParam.Constants.RegisterSpace = parmDesc.registerSpace;
                d3dParam.Constants.Num32BitValues = parmDesc.num32BitValues;
                d3dParameters.push_back(d3dParam);
                continue;
            }

            if (is_descriptor_table_parameter(parmDesc.type))
            {
                D3D12_DESCRIPTOR_RANGE1 descriptorRange{};
                descriptorRange.RangeType = convert_descriptor_range_type(parmDesc.type);
                descriptorRange.NumDescriptors =
                    parmDesc.descriptorCount == 0
                    ? UINT_MAX
                    : parmDesc.descriptorCount;
                descriptorRange.BaseShaderRegister = parmDesc.shaderRegister;
                descriptorRange.RegisterSpace = parmDesc.registerSpace;
                descriptorRange.Flags =
                    D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
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
            d3dParam.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
            d3dParameters.push_back(d3dParam);
        }

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

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1.NumParameters =
            static_cast<UINT>(d3dParameters.size());
        rootSignatureDesc.Desc_1_1.pParameters = d3dParameters.data();
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = &staticSampler;
        rootSignatureDesc.Desc_1_1.Flags = k_rootSignatureFlags;

        ComPtr<ID3DBlob> serializedRootSig;
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeVersionedRootSignature(
            &rootSignatureDesc,
            &serializedRootSig,
            &errorBlob);
        if (FAILED(hr))
        {
            g_lastRootSignatureSerializeError =
                "Failed to serialize root signature";
            if (!desc.name.empty())
            {
                g_lastRootSignatureSerializeError += ": ";
                g_lastRootSignatureSerializeError += desc.name;
            }
            if (errorBlob)
            {
                g_lastRootSignatureSerializeError += " - ";
                g_lastRootSignatureSerializeError +=
                    static_cast<const char*>(errorBlob->GetBufferPointer());
            }
            ::OutputDebugStringA(g_lastRootSignatureSerializeError.c_str());
            ::OutputDebugStringA("\n");
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                g_lastRootSignatureSerializeError);
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
        // Shader blob は PSO と分離して cache し、同じ shader を複数 pipeline から再利用できるようにする。
        // HLSLCompiler の失敗を Result で受け、失敗時に無効ハンドルを成功扱いしない
        ShaderBlobRecord blobRecord{};
        blobRecord.desc = desc;
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
