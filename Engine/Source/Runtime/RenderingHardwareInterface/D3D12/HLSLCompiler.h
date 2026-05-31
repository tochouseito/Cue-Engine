#pragma once

/// ************************************************************************************
/// HLSLコンパイラー
/// ************************************************************************************

// === RHI includes ===
#include <ShaderCompiler.h>

// === DirectX 12 includes ===
#include "DX12Common.h"

namespace Cue::RHI::DX12
{
    class HLSLCompiler : public Cue::RHI::ShaderCompiler
    {
    public:
        /// @brief コンストラクタ
        HLSLCompiler();
        /// @brief デストラクタ
        ~HLSLCompiler() override = default;

        Result compile_shader_raw(const ShaderCompileDesc& desc, ComPtr<IDxcBlob>* outBlob);
    private:
        ComPtr<IDxcUtils> m_dxcUtils = nullptr;
        ComPtr<IDxcCompiler3> m_dxcCompiler = nullptr;
        ComPtr<IDxcIncludeHandler> m_dxcIncludeHandler = nullptr;
        std::string m_cachePath = {};
    };
}
