#pragma once
#include "GraphicsCommon.h"
#include "ShaderCompiler.h"

namespace Cue::GraphicsCore
{
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

    enum class DSVFormat : uint8_t
    {
        D24_UNorm_S8_UInt,
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

    class PipelineManager
    {
    public:
        PipelineManager() = default;
        virtual ~PipelineManager() = default;

        virtual PipelineStateHandle create_graphics_pipeline(const GraphicsPipelineStateDesc& desc) = 0;

        virtual ShaderBlobHandle compile_shader(const ShaderCompileDesc& desc) = 0;
    private:
    };
}
