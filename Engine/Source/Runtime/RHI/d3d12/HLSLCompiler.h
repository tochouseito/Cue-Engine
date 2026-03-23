#pragma once

// === RHI includes ===
#include <ShaderCompiler.h>

// === DirectX 12 includes ===
#include "stdafx.h"

namespace Cue::RHI::DX12
{
    class HLSLCompiler : public Cue::RHI::ShaderCompiler
    {
    public:
        /// @brief コンストラクタ
        HLSLCompiler();
        /// @brief デストラクタ
        ~HLSLCompiler() override = default;

        ComPtr<IDxcBlob> compile_shader_raw(const ShaderCompileDesc& desc);
    private:
        ComPtr<IDxcUtils> m_dxcUtils = nullptr;
        ComPtr<IDxcCompiler3> m_dxcCompiler = nullptr;
        ComPtr<IDxcIncludeHandler> m_dxcIncludeHandler = nullptr;
        std::string m_cachePath = {};
    };
}
