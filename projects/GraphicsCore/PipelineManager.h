#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    enum class RootParameterType : uint8_t
    {
        CBV,
        SRV,
        UAV,
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
        std::string_view name;
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

    enum class PrimitiveTopologyType : uint8_t
    {
        Triangle,
    };

    struct GraphicsPipelineStateDesc final
    {
        std::string_view name = {};
        RootSignatureHandle rootSignatureHandle = {};
        ShaderBlobHandle vsHandle = {};
        ShaderBlobHandle psHandle = {};
        std::vector<InputElementDesc> inputElements = {};
        std::vector<BlendMode> blendMode = { BlendMode::None };
        RasterizerStateDesc rasterizerState = {};
        DepthStencilStateDesc depthStencilState = {};
        DSVFormat dsvFormat = DSVFormat::D24_UNorm_S8_UInt;
        PrimitiveTopologyType primitiveTopologyType = PrimitiveTopologyType::Triangle;
        std::vector<ColorFormat> rtvFormats = {};
    };

    class IPipelineManager
    {
    public:
        IPipelineManager() = default;
        virtual ~IPipelineManager() = default;
        virtual Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& outHandle) = 0;
        
        virtual Result destroy_pipeline(const PipelineStateHandle& handle) = 0;
        virtual Result get_pipeline(ResourceNameId nameId, PipelineStateHandle& outHandle) = 0;

        virtual Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& outHandle) = 0;
        virtual Result destroy_root_signature(const RootSignatureHandle& handle) = 0;
        virtual Result get_root_signature(ResourceNameId nameId, RootSignatureHandle& outHandle) = 0;

        virtual Result compile_shader(const ShaderCompileDesc& desc, ShaderBlobHandle& outHandle) = 0;
        virtual Result destroy_shader(const ShaderBlobHandle& handle) = 0;
        virtual Result get_shader(ResourceNameId nameId, ShaderBlobHandle& outHandle) = 0;
    };
}
