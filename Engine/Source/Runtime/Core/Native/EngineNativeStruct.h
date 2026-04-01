#pragma once

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::Core::Native
{
    /// @brief 1 メッシュ分の頂点データです。
    struct MeshData
    {
        std::string name;                         // メッシュ名
        std::vector<Math::float4> positions;     // 位置配列
        std::vector<Math::float2> uvs;           // UV座標配列
        std::vector<Math::float3> normals;       // 法線配列
        std::vector<std::uint32_t> indices; // インデックスデータ配列

        /// @brief 頂点数を返します。
        /// @return 位置配列の要素数です。
        [[nodiscard]] uint32_t vertex_count() const noexcept
        {
            return static_cast<uint32_t>(positions.size());
        }
    };

    /// @brief モデル全体のメッシュ集合です。
    struct ModelData
    {
        std::vector<MeshData> meshes; // メッシュデータ配列
    };
}
