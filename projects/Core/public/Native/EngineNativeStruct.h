#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include <CueMath.h>

namespace Cue::Core::Native
{
    struct VertexData
    {
        Math::float4 position;   // 位置
        Math::float2 uv;         // UV座標
        Math::float3 normal;     // 法線
    };

    struct MeshData
    {
        std::string name;               // メッシュ名
        std::vector<VertexData> vertices; // 頂点データ配列
        std::vector<std::uint32_t> indices; // インデックスデータ配列
    };

    struct ModelData
    {
        std::vector<MeshData> meshes; // メッシュデータ配列
    };
}
