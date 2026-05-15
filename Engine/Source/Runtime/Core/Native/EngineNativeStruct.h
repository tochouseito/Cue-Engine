#pragma once

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::Core::Native
{
    inline constexpr uint32_t k_maxSkinInfluenceCount = 4u;
    inline constexpr uint32_t k_invalidModelMaterialIndex =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr uint32_t k_invalidAnimationIndex =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr int32_t k_invalidJointIndex = -1;

    struct SkinInfluenceData
    {
        uint32_t jointIndices[k_maxSkinInfluenceCount] = {};
        float weights[k_maxSkinInfluenceCount] = {};
    };

    struct SkeletonJointData
    {
        std::string name;
        Math::float4x4 inverseBindMatrix = Math::float4x4::identity();
        Math::float4x4 localBindMatrix = Math::float4x4::identity();
        int32_t parentIndex = k_invalidJointIndex;
    };

    struct VectorKeyframeData
    {
        float time = 0.0f;
        Math::float3 value = Math::float3::zero();
    };

    struct QuaternionKeyframeData
    {
        float time = 0.0f;
        Math::Quaternion value = Math::Quaternion::identity();
    };

    struct AnimationChannelData
    {
        std::string targetName;
        uint32_t jointIndex = k_invalidAnimationIndex;
        std::vector<VectorKeyframeData> translations;
        std::vector<QuaternionKeyframeData> rotations;
        std::vector<VectorKeyframeData> scales;
    };

    struct AnimationClipData
    {
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 1.0f;
        std::vector<AnimationChannelData> channels;
    };

    /// @brief 1 メッシュ分の頂点データです。
    struct MeshData
    {
        std::string name;                         // メッシュ名
        std::vector<Math::float4> positions;     // 位置配列
        std::vector<Math::float2> uvs;           // UV座標配列
        std::vector<Math::float3> normals;       // 法線配列
        std::vector<SkinInfluenceData> skinInfluences; // スキニング用頂点影響
        std::vector<std::uint32_t> indices; // インデックスデータ配列

        /// @brief 頂点数を返します。
        /// @return 位置配列の要素数です。
        [[nodiscard]] uint32_t vertex_count() const noexcept
        {
            return static_cast<uint32_t>(positions.size());
        }
    };

    /// @brief インポート元モデルに含まれるマテリアル情報です。
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

    /// @brief モデル内の描画単位です。
    struct ModelRenderPartData
    {
        std::string name; // 描画単位名
        uint32_t meshIndex = 0;
        uint32_t materialIndex = k_invalidModelMaterialIndex;
        Math::float4x4 localTransform = Math::float4x4::identity();
    };

    /// @brief モデル全体のメッシュ集合です。
    struct ModelData
    {
        std::vector<MeshData> meshes; // メッシュデータ配列
        std::vector<ImportedMaterialData> materials;
        std::vector<ModelRenderPartData> renderParts;
        std::vector<SkeletonJointData> skeletonJoints;
        std::vector<AnimationClipData> animationClips;
    };
}
