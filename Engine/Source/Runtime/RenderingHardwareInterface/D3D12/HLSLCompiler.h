#pragma once

/// ************************************************************************************
/// HLSLコンパイラー
/// ************************************************************************************

// === RHI includes ===
#include <ShaderCompiler.h>

// === Core includes ===
#include <IO/IFileSystem.h>

// === DirectX 12 includes ===
#include "DX12Common.h"

namespace Cue::RHI::DX12
{
    /// @brief DXC を使って HLSL を DXIL blob に変換する compiler 実装。
    /// @details ShaderCompiler の抽象 API から、D3D12 が要求する IDxcBlob
    /// を取得するための入口。
    class HLSLCompiler : public Cue::RHI::ShaderCompiler
    {
      public:
        /// @brief コンストラクタ
        HLSLCompiler();
        /// @brief デストラクタ
        ~HLSLCompiler() override = default;

        void set_file_system(Core::IO::IFileSystem* fileSystem) noexcept;
        Result compile_shader_raw(const ShaderCompileDesc& desc,
                                  ComPtr<IDxcBlob>* outBlob);

      private:
        // DXC COM object 群。初期化後は各 compile 呼び出しで再利用する。
        ComPtr<IDxcUtils> m_dxcUtils = nullptr;
        ComPtr<IDxcCompiler3> m_dxcCompiler = nullptr;
        ComPtr<IDxcIncludeHandler> m_dxcIncludeHandler = nullptr;

        // 将来の shader cache 用。現状は compile 結果の保存先として予約している。
        std::string m_cachePath = {};
        Core::IO::IFileSystem* m_fileSystem = nullptr;
    };
} // namespace Cue::RHI::DX12
