// HLSLCompiler の役割と公開要素を定義する

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

        Result compile_shader_raw(const ShaderCompileDesc& desc, comPtr<IDxcBlob>* outBlob);
    private:
        comPtr<IDxcUtils> m_dxcUtils = nullptr;
        comPtr<IDxcCompiler3> m_dxcCompiler = nullptr;
        comPtr<IDxcIncludeHandler> m_dxcIncludeHandler = nullptr;
        std::string m_cachePath = {};
    };
}
