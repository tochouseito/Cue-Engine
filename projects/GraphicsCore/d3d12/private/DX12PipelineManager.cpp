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
        for (size_t i = 0; i < modes.size() && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            D3D12_RENDER_TARGET_BLEND_DESC& rtDesc = desc.RenderTarget[i];
            switch (modes[i])
            {
            case BlendMode::None:
                rtDesc.BlendEnable = false;
                break;
            case BlendMode::Normal:
                rtDesc.BlendEnable = true;
                break;
            default:
                rtDesc.BlendEnable = false;
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
}

namespace Cue::GraphicsCore::DX12
{
    PipelineStateHandle DX12PipelineManager::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc)
    {
        return PipelineStateHandle();
    }
} // namespace Cue::GraphicsCore::DX12
