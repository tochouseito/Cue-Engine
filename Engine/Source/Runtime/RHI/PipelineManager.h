#pragma once

// === RHI Includes ===
#include "RHICommon.h"
#include "ShaderCompiler.h"

namespace Cue::RHI
{
    enum class RootParameterType : uint8_t
    {
        CBV,
        SRV,
        UAV,
        DescriptorTableCBV,
        DescriptorTableSRV,
        DescriptorTableUAV,
        _32BitConstants
    };

    enum class ShaderVisibility : uint8_t
    {
        All,
        Vertex,
        Pixel
    };

    struct RootParameterDesc
    {
        RootParameterType type;
        ShaderVisibility visibility;
        uint32_t shaderRegister;
    };

    struct RootSignatureDesc
    {
        std::string name;
        std::vector<RootParameterDesc> parameters;
    };

    enum class InputElementFormat : uint8_t
    {
        R32G32B32A32_Float,
        R32G32B32_Float,
        R32G32_Float,
        R32_Float,
    };

    struct InputElementDesc final
    {
        std::string semanticName = {};
        uint32_t semanticIndex = 0;
        InputElementFormat format = InputElementFormat::R32G32B32A32_Float;
        uint32_t inputSlot = 0;
        uint32_t alignedByteOffset = 0;
    };

    enum class BlendMode : uint8_t
    {
        None,
        Normal
    };

    enum class CullMode : uint8_t
    {
        None,
        Front,
        Back
    };

    enum class FillMode : uint8_t
    {
        Solid,
        Wireframe
    };

    struct RasterizerStateDesc final
    {
        CullMode cullMode = CullMode::Back;
        FillMode fillMode = FillMode::Solid;
    };

    enum class DepthWriteMask : uint8_t
    {
        Zero,
        All
    };

    enum class ComparisonFunc : uint8_t
    {
        LessEqual,
    };

    struct DepthStencilStateDesc final
    {
        bool depthEnable = true;
        DepthWriteMask depthWriteMask = DepthWriteMask::All;
        ComparisonFunc depthFunc = ComparisonFunc::LessEqual;
    };

    struct GraphicsPipelineStateDesc
    {
        std::string name = {};
        rootSignatureHandle rootSignatureHandle = {};
        shaderBlobHandle vsHandle = {};
        shaderBlobHandle psHandle = {};
        std::vector<InputElementDesc> inputElements = {};
        std::vector<BlendMode> blendMode = { BlendMode::None };
        RasterizerStateDesc rasterizerState = {};
        DepthStencilStateDesc depthStencilState = {};
        ColorFormat dsvFormat = ColorFormat::D24_UNorm_S8_UInt;
        PrimitiveTopologyType primitiveTopologyType = PrimitiveTopologyType::Triangle;
        std::vector<ColorFormat> rtvFormats = {};
    };

    struct ComputePipelineStateDesc
    {
        std::string name = {};
        rootSignatureHandle rootSignatureHandle = {};
        shaderBlobHandle csHandle = {};
    };

    class IPipelineManager
    {
    public:
        IPipelineManager() = default;
        // コピー禁止
        IPipelineManager(const IPipelineManager&) = delete;
        IPipelineManager& operator=(const IPipelineManager&) = delete;
        // ムーブ禁止
        IPipelineManager(IPipelineManager&&) = delete;
        IPipelineManager& operator=(IPipelineManager&&) = delete;
        virtual ~IPipelineManager() = default;

        virtual Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, pipelineStateHandle& out) = 0;
        virtual Result destroy_graphics_pipeline(pipelineStateHandle handle) = 0;
        virtual Result get_graphics_pipeline(std::string_view name, pipelineStateHandle& out) = 0;
        virtual Result create_compute_pipeline(const ComputePipelineStateDesc& desc, pipelineStateHandle& out) = 0;
        virtual Result destroy_compute_pipeline(pipelineStateHandle handle) = 0;
        virtual Result get_compute_pipeline(std::string_view name, pipelineStateHandle& out) = 0;

        virtual Result create_root_signature(const RootSignatureDesc& desc, rootSignatureHandle& out) = 0;
        virtual Result destroy_root_signature(rootSignatureHandle handle) = 0;
        virtual Result get_root_signature(std::string_view name, rootSignatureHandle& out) = 0;

        virtual Result create_shader_blob(const ShaderCompileDesc& desc, shaderBlobHandle& out) = 0;
        virtual Result destroy_shader_blob(shaderBlobHandle handle) = 0;
        virtual Result get_shader_blob(std::string_view name, shaderBlobHandle& out) = 0;
    };
}
