#pragma once
#include "stdafx.h"
#include <ShaderCompiler.h>

namespace Cue::GraphicsCore::DX12
{
    std::wstring shader_profile_to_wstring(D3D_SHADER_MODEL model);

    class HLSLCompiler : public Cue::GraphicsCore::ShaderCompiler
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
