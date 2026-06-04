#pragma once

/// ****************************************************************************
/// エンジン内部で使用する構造体
/// ****************************************************************************

// === C++ include ===
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::Core::Native
{
    // --- 無効なインデックス定数 ---
    inline constexpr uint32_t k_maxSkinInfluenceCount = 4u;
    inline constexpr uint32_t k_invalidModelMaterialIndex =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr uint32_t k_invalidAnimationIndex =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr int32_t k_invalidJointIndex = -1;

    /// @brief 1 メッシュ分の頂点データ
    struct MeshData
    {
        std::string name;                         // メッシュ名
        std::vector<Math::float4> positions;     // 位置配列
        std::vector<Math::float2> uvs;           // UV座標配列
        std::vector<Math::float3> normals;       // 法線配列
        std::vector<std::uint32_t> indices; // インデックスデータ配列

        /// @brief 頂点数を返す
        /// @return 位置配列の要素数
        [[nodiscard]] uint32_t vertex_count() const noexcept
        {
            return static_cast<uint32_t>(positions.size());
        }
    };

    /// @brief インポート元モデルに含まれるマテリアル情報
    struct ImportedMaterialData
    {
        std::string name; // マテリアル名
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        std::string textureName;
        std::string sourceTexturePath;
        float shininess = 32.0f;
        bool isTextureUsed = false;
        bool usesReflectionSkybox = false;
    };

    /// @brief モデル内の描画単位
    struct ModelRenderPartData
    {
        std::string name; // 描画単位名
        uint32_t meshIndex = 0;
        std::vector<uint32_t> lodMeshIndices{};
        uint32_t occluderMeshIndex = k_invalidModelMaterialIndex;
        uint32_t materialIndex = k_invalidModelMaterialIndex;
        uint32_t jointIndex = k_invalidAnimationIndex;
        Math::float4x4 localTransform = Math::float4x4::identity();
    };

    /// @brief モデル全体のメッシュ集合
    struct ModelData
    {
        std::vector<MeshData> meshes; // メッシュデータ配列
        std::vector<ImportedMaterialData> materials;
        std::vector<ModelRenderPartData> renderParts;
    };
}
