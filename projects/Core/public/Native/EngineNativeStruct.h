#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include <CueMath.h>

namespace Cue::Core::Native
{
    struct MeshData
    {
        std::string name;                         // メッシュ名
        std::vector<Math::float4> positions;     // 位置配列
        std::vector<Math::float2> uvs;           // UV座標配列
        std::vector<Math::float3> normals;       // 法線配列
        std::vector<std::uint32_t> indices; // インデックスデータ配列

        [[nodiscard]] uint32_t vertex_count() const noexcept
        {
            return static_cast<uint32_t>(positions.size());
        }
    };

    struct ModelData
    {
        std::vector<MeshData> meshes; // メッシュデータ配列
    };

    struct LocalTransform
    {
        Math::float3 position{ 0.0f, 0.0f, 0.0f }; // ローカル位置
        Math::float3 rotation{ 0.0f, 0.0f, 0.0f }; // ローカル回転（オイラー角）
        Math::float3 scale{ 1.0f, 1.0f, 1.0f };    // ローカルスケール
    };

    struct ObjectTransformGpu
    {
        Math::float4x4 worldMatrix; // ワールド変換行列
    };
}
