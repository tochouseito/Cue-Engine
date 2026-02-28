#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    struct ShaderCompileDesc final
    {
        std::string name = {};                 ///< シェーダー名（キャッシュ用）
        std::string filePath = {};               ///< シェーダーファイルのパス
        std::string entryPoint = {};            ///< エントリーポイント名
        std::string targetProfile = {};         ///< ターゲットプロファイル名
        bool enableDebugInfo = true;          ///< デバッグ情報を有効にするかどうか

        explicit operator bool() const noexcept
        {
            if (filePath.empty() || entryPoint.empty() || targetProfile.empty())
            {
                return false;
            }
            return true;
        }
    };

    class ShaderCompiler
    {
    public:
        ShaderCompiler() = default;
        virtual ~ShaderCompiler() = default;
    };
} // namespace Cue::GraphicsCore
