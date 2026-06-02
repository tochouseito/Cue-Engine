// ShaderCompiler の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <RHICommon.h>

namespace Cue::RHI
{
    struct ShaderCompileDesc final
    {
        std::string name = {};                 ///< シェーダー名（キャッシュ用）
        std::string filePath = {};               ///< シェーダーファイルのパス
        std::string entryPoint = {};            ///< エントリーポイント名
        std::string targetProfile = {};         ///< ターゲットプロファイル名
        bool enableDebugInfo = true;          ///< デバッグ情報を有効にするかどうか

        // シェーダーコンパイルの有効性を評価するための明示的な変換演算子
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
}
