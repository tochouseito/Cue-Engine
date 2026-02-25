#pragma once
#include "stdafx.h"
#include "RenderDevice.h"

namespace Cue::GraphicsCore::DX12
{
    class SwapChain final
    {
    public:
        SwapChain(RenderDevice& renderDevice)
            : m_renderDevice(renderDevice)
        {
        }
        ~SwapChain() = default;
    private:
        RenderDevice& m_renderDevice;
    };
}
