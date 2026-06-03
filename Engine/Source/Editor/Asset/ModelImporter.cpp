#include "ModelImporter.h"

// === Core includes ===
#include <IO/Logger.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// === ThirdParty includes ===
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer.h>

namespace Cue::Editor
{
namespace
{
[[nodiscard]] std::string make_mesh_name(const aiMesh &mesh,
                                         std::string_view modelName,
                                         uint32_t meshIndex) noexcept
{
    if (mesh.mName.length > 0 && mesh.mName.C_Str() != nullptr)
    {
        return std::string(mesh.mName.C_Str(), mesh.mName.length);
    }

    if (meshIndex == 0)
    {
        return std::string(modelName);
    }

    return std::string(modelName) + "_" + std::to_string(meshIndex);
}

[[nodiscard]] std::string make_material_name(const aiMaterial &material,
                                             uint32_t materialIndex) noexcept
{
    aiString name{};
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0 &&
        name.C_Str() != nullptr)
    {
        return std::string(name.C_Str(), name.length);
    }

    return "Material_" + std::to_string(materialIndex);
}

[[nodiscard]] std::string make_texture_asset_name(
    const aiString &texturePath) noexcept
{
    if (texturePath.length == 0 || texturePath.C_Str() == nullptr)
    {
        return {};
    }

    const Core::IO::Path sourcePath(
        std::string(texturePath.C_Str(), texturePath.length));
    return Core::IO::Path::join(Core::IO::Path("Textures"),
                                Core::IO::Path(sourcePath.stem() + ".dds"))
        .utf8();
}

[[nodiscard]] bool is_embedded_texture_path(
    const aiString &texturePath) noexcept
{
    return texturePath.length > 0 && texturePath.C_Str() != nullptr &&
           texturePath.C_Str()[0] == '*';
}

[[nodiscard]] Core::Native::ImportedMaterialData import_material(
    const aiMaterial &material, uint32_t materialIndex) noexcept
{
    Core::Native::ImportedMaterialData materialData{};
    materialData.name = make_material_name(material, materialIndex);

    aiColor4D diffuseColor{};
    if (aiGetMaterialColor(&material, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor) ==
        AI_SUCCESS)
    {
        materialData.color = Math::float4(diffuseColor.r, diffuseColor.g,
                                          diffuseColor.b, diffuseColor.a);
    }
    if (aiGetMaterialColor(&material, AI_MATKEY_BASE_COLOR, &diffuseColor) ==
        AI_SUCCESS)
    {
        materialData.color = Math::float4(diffuseColor.r, diffuseColor.g,
                                          diffuseColor.b, diffuseColor.a);
    }

    float shininess = 32.0f;
    if (material.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
    {
        materialData.shininess = shininess;
    }

    aiString texturePath{};
    aiReturn textureResult =
        material.GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);
    if (textureResult != AI_SUCCESS)
    {
        textureResult =
            material.GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath);
    }
    if (textureResult == AI_SUCCESS && !is_embedded_texture_path(texturePath))
    {
        materialData.sourceTexturePath =
            std::string(texturePath.C_Str(), texturePath.length);
        materialData.textureName = make_texture_asset_name(texturePath);
        materialData.isTextureUsed = !materialData.textureName.empty();
    }

    return materialData;
}

[[nodiscard]] Math::float4x4 convert_node_transform(
    const aiMatrix4x4 &transform) noexcept
{
    aiVector3D scale{};
    aiQuaternion rotation{};
    aiVector3D translation{};
    transform.Decompose(scale, rotation, translation);

    return Math::make_affine_matrix(
        Math::float3(scale.x, scale.y, scale.z),
        Math::Quaternion(rotation.x, -rotation.y, -rotation.z, rotation.w),
        Math::float3(-translation.x, translation.y, translation.z));
}

[[nodiscard]] Math::float4x4 convert_inverse_bind_matrix(
    const aiMatrix4x4 &offsetMatrix) noexcept
{
    aiMatrix4x4 bindPoseMatrix = offsetMatrix;
    bindPoseMatrix.Inverse();
    return Math::float4x4::inverse(convert_node_transform(bindPoseMatrix));
}

[[nodiscard]] Math::float3 convert_translation(const aiVector3D &value) noexcept
{
    return Math::float3(-value.x, value.y, value.z);
}

[[nodiscard]] Math::float3 convert_scale(const aiVector3D &value) noexcept
{
    return Math::float3(value.x, value.y, value.z);
}

[[nodiscard]] Math::Quaternion convert_rotation(
    const aiQuaternion &value) noexcept
{
    return Math::Quaternion(value.x, -value.y, -value.z, value.w);
}

[[nodiscard]] std::string ai_name(const aiString &value)
{
    if (value.length == 0 || value.C_Str() == nullptr)
    {
        return {};
    }

    return std::string(value.C_Str(), value.length);
}

[[nodiscard]] std::string node_name(const aiNode &node)
{
    return ai_name(node.mName);
}

[[nodiscard]] bool scene_has_bones(const aiScene &a_scene) noexcept
{
    for (uint32_t meshIndex = 0; meshIndex < a_scene.mNumMeshes; ++meshIndex)
    {
        const aiMesh *mesh = a_scene.mMeshes[meshIndex];
        if (mesh != nullptr && mesh->mNumBones > 0)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool scene_has_animations(const aiScene &a_scene) noexcept
{
    return a_scene.mNumAnimations > 0 && a_scene.mAnimations != nullptr;
}

[[nodiscard]] std::string make_render_part_name(
    const aiNode &node, const Core::Native::MeshData &meshData,
    uint32_t partIndex)
{
    if (node.mName.length > 0 && node.mName.C_Str() != nullptr)
    {
        return std::string(node.mName.C_Str(), node.mName.length);
    }

    if (!meshData.name.empty())
    {
        return meshData.name;
    }

    return "Part_" + std::to_string(partIndex);
}

struct MeshoptVertex final
{
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float pw = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
};

struct MeshoptPosition final
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct LodGenerationStats final
{
    size_t sourceVertexCount = 0;
    size_t weldedVertexCount = 0;
    size_t targetIndexCount = 0;
    float lodError = 0.0f;
    bool usedSloppy = false;
};

[[nodiscard]] std::vector<MeshoptVertex> pack_vertices(
    const Core::Native::MeshData &meshData)
{
    std::vector<MeshoptVertex> vertices(meshData.positions.size());
    for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
    {
        const Math::float4 &position = meshData.positions[vertexIndex];
        MeshoptVertex &vertex = vertices[vertexIndex];
        vertex.px = position.x;
        vertex.py = position.y;
        vertex.pz = position.z;
        vertex.pw = position.w;

        if (vertexIndex < meshData.uvs.size())
        {
            vertex.u = meshData.uvs[vertexIndex].x;
            vertex.v = meshData.uvs[vertexIndex].y;
        }
        if (vertexIndex < meshData.normals.size())
        {
            vertex.nx = meshData.normals[vertexIndex].x;
            vertex.ny = meshData.normals[vertexIndex].y;
            vertex.nz = meshData.normals[vertexIndex].z;
        }
    }
    return vertices;
}

void unpack_vertices(const std::vector<MeshoptVertex> &vertices,
                     Core::Native::MeshData &meshData)
{
    meshData.positions.resize(vertices.size());
    meshData.uvs.resize(vertices.size());
    meshData.normals.resize(vertices.size());
    for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
    {
        const MeshoptVertex &vertex = vertices[vertexIndex];
        meshData.positions[vertexIndex] =
            Math::float4(vertex.px, vertex.py, vertex.pz, vertex.pw);
        meshData.uvs[vertexIndex] = Math::float2(vertex.u, vertex.v);
        meshData.normals[vertexIndex] =
            Math::float3(vertex.nx, vertex.ny, vertex.nz);
    }
}

[[nodiscard]] Core::Native::MeshData remap_mesh(
    const Core::Native::MeshData &sourceMesh)
{
    Core::Native::MeshData result = sourceMesh;
    if (sourceMesh.positions.empty() || sourceMesh.indices.empty())
    {
        return result;
    }

    std::vector<MeshoptVertex> sourceVertices = pack_vertices(sourceMesh);
    std::vector<unsigned int> remap(sourceVertices.size());
    const size_t uniqueVertexCount = meshopt_generateVertexRemap(
        remap.data(), sourceMesh.indices.data(), sourceMesh.indices.size(),
        sourceVertices.data(), sourceVertices.size(), sizeof(MeshoptVertex));

    std::vector<uint32_t> remappedIndices(sourceMesh.indices.size());
    std::vector<MeshoptVertex> remappedVertices(uniqueVertexCount);
    meshopt_remapIndexBuffer(remappedIndices.data(), sourceMesh.indices.data(),
                             sourceMesh.indices.size(), remap.data());
    meshopt_remapVertexBuffer(remappedVertices.data(), sourceVertices.data(),
                              sourceVertices.size(), sizeof(MeshoptVertex),
                              remap.data());

    result.indices = std::move(remappedIndices);
    unpack_vertices(remappedVertices, result);
    return result;
}

[[nodiscard]] Core::Native::MeshData optimize_mesh_for_render(
    const Core::Native::MeshData &sourceMesh)
{
    Core::Native::MeshData result = sourceMesh;
    if (sourceMesh.positions.empty() || sourceMesh.indices.empty())
    {
        return result;
    }

    std::vector<MeshoptVertex> sourceVertices = pack_vertices(sourceMesh);

    std::vector<uint32_t> vertexCacheIndices(sourceMesh.indices.size());
    meshopt_optimizeVertexCache(
        vertexCacheIndices.data(), sourceMesh.indices.data(),
        sourceMesh.indices.size(), sourceVertices.size());

    std::vector<uint32_t> overdrawIndices(vertexCacheIndices.size());
    meshopt_optimizeOverdraw(overdrawIndices.data(), vertexCacheIndices.data(),
                             vertexCacheIndices.size(), &sourceVertices[0].px,
                             sourceVertices.size(), sizeof(MeshoptVertex),
                             1.05f);

    std::vector<MeshoptVertex> fetchVertices(sourceVertices.size());
    const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
        fetchVertices.data(), overdrawIndices.data(), overdrawIndices.size(),
        sourceVertices.data(), sourceVertices.size(), sizeof(MeshoptVertex));
    fetchVertices.resize(fetchedVertexCount);

    result.indices = std::move(overdrawIndices);
    unpack_vertices(fetchVertices, result);
    return result;
}

[[nodiscard]] Core::Native::MeshData optimize_mesh(
    const Core::Native::MeshData &sourceMesh)
{
    Core::Native::MeshData remappedMesh = remap_mesh(sourceMesh);
    return optimize_mesh_for_render(remappedMesh);
}

[[nodiscard]] Core::Native::MeshData make_position_welded_mesh(
    const Core::Native::MeshData &sourceMesh, size_t &outUniqueVertexCount)
{
    Core::Native::MeshData result = sourceMesh;
    outUniqueVertexCount = sourceMesh.positions.size();
    if (sourceMesh.positions.empty() || sourceMesh.indices.empty())
    {
        return result;
    }

    std::vector<MeshoptPosition> positions(sourceMesh.positions.size());
    for (size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex)
    {
        positions[vertexIndex].x = sourceMesh.positions[vertexIndex].x;
        positions[vertexIndex].y = sourceMesh.positions[vertexIndex].y;
        positions[vertexIndex].z = sourceMesh.positions[vertexIndex].z;
    }

    std::vector<unsigned int> remap(positions.size());
    const size_t uniqueVertexCount = meshopt_generateVertexRemap(
        remap.data(), sourceMesh.indices.data(), sourceMesh.indices.size(),
        positions.data(), positions.size(), sizeof(MeshoptPosition));
    outUniqueVertexCount = uniqueVertexCount;

    std::vector<uint32_t> remappedIndices(sourceMesh.indices.size());
    meshopt_remapIndexBuffer(remappedIndices.data(), sourceMesh.indices.data(),
                             sourceMesh.indices.size(), remap.data());

    result.positions.assign(uniqueVertexCount,
                            Math::float4(0.0f, 0.0f, 0.0f, 1.0f));
    result.uvs.assign(uniqueVertexCount, Math::float2(0.0f, 0.0f));
    result.normals.assign(uniqueVertexCount, Math::float3(0.0f, 1.0f, 0.0f));

    std::vector<uint8_t> filled(uniqueVertexCount, 0u);
    for (size_t sourceVertexIndex = 0;
         sourceVertexIndex < sourceMesh.positions.size(); ++sourceVertexIndex)
    {
        const uint32_t remappedVertexIndex = remap[sourceVertexIndex];
        if (remappedVertexIndex >= uniqueVertexCount ||
            filled[remappedVertexIndex] != 0u)
        {
            continue;
        }

        result.positions[remappedVertexIndex] =
            sourceMesh.positions[sourceVertexIndex];
        if (sourceVertexIndex < sourceMesh.uvs.size())
        {
            result.uvs[remappedVertexIndex] = sourceMesh.uvs[sourceVertexIndex];
        }
        if (sourceVertexIndex < sourceMesh.normals.size())
        {
            result.normals[remappedVertexIndex] =
                sourceMesh.normals[sourceVertexIndex];
        }
        filled[remappedVertexIndex] = 1u;
    }

    result.indices = std::move(remappedIndices);
    return result;
}

[[nodiscard]] Core::Native::MeshData make_billboard_lod_mesh(
    const Core::Native::MeshData &baseMesh)
{
    Core::Native::MeshData billboardMesh{};
    billboardMesh.name = baseMesh.name + "_lod4_billboard";
    if (baseMesh.positions.empty())
    {
        return billboardMesh;
    }

    Math::float3 minPosition(baseMesh.positions[0].x, baseMesh.positions[0].y,
                             baseMesh.positions[0].z);
    Math::float3 maxPosition = minPosition;
    for (const Math::float4 &position : baseMesh.positions)
    {
        minPosition.x = (std::min)(minPosition.x, position.x);
        minPosition.y = (std::min)(minPosition.y, position.y);
        minPosition.z = (std::min)(minPosition.z, position.z);
        maxPosition.x = (std::max)(maxPosition.x, position.x);
        maxPosition.y = (std::max)(maxPosition.y, position.y);
        maxPosition.z = (std::max)(maxPosition.z, position.z);
    }

    const Math::float3 center = (minPosition + maxPosition) * 0.5f;
    float radiusSq = 0.0f;
    for (const Math::float4 &position : baseMesh.positions)
    {
        const float dx = position.x - center.x;
        const float dy = position.y - center.y;
        const float dz = position.z - center.z;
        radiusSq = (std::max)(radiusSq, dx * dx + dy * dy + dz * dz);
    }
    const float radius = std::sqrt(radiusSq);

    billboardMesh.positions = {
        Math::float4(-radius, radius, 0.0f, 1.0f),
        Math::float4(radius, radius, 0.0f, 1.0f),
        Math::float4(radius, -radius, 0.0f, 1.0f),
        Math::float4(-radius, -radius, 0.0f, 1.0f),
    };
    billboardMesh.uvs = {
        Math::float2(0.0f, 0.0f),
        Math::float2(1.0f, 0.0f),
        Math::float2(1.0f, 1.0f),
        Math::float2(0.0f, 1.0f),
    };
    billboardMesh.normals = {
        Math::float3(0.0f, 0.0f, 1.0f),
        Math::float3(0.0f, 0.0f, 1.0f),
        Math::float3(0.0f, 0.0f, 1.0f),
        Math::float3(0.0f, 0.0f, 1.0f),
    };
    billboardMesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return billboardMesh;
}

[[nodiscard]] float lod_target_error(uint32_t lodIndex) noexcept
{
    if (lodIndex >= 3u)
    {
        return 0.30f;
    }
    if (lodIndex == 2u)
    {
        return 0.15f;
    }
    return 0.05f;
}

[[nodiscard]] Core::Native::MeshData generate_lod_mesh(
    const Core::Native::MeshData &baseMesh, float indexRatio, uint32_t lodIndex,
    LodGenerationStats &outStats)
{
    Core::Native::MeshData lodMesh = baseMesh;
    outStats = {};
    outStats.sourceVertexCount = baseMesh.positions.size();
    if (baseMesh.indices.size() < 3 || baseMesh.positions.empty())
    {
        return lodMesh;
    }

    size_t weldedVertexCount = 0;
    Core::Native::MeshData weldedMesh =
        make_position_welded_mesh(baseMesh, weldedVertexCount);
    outStats.weldedVertexCount = weldedVertexCount;

    size_t targetIndexCount =
        static_cast<size_t>(static_cast<double>(baseMesh.indices.size()) *
                            static_cast<double>(indexRatio));
    targetIndexCount = (std::max<size_t>)(3, targetIndexCount);
    targetIndexCount = (targetIndexCount / 3) * 3;
    outStats.targetIndexCount = targetIndexCount;

    std::vector<uint32_t> simplified(weldedMesh.indices.size());
    float lodError = 0.0f;
    const size_t simplifiedIndexCount = meshopt_simplify(
        simplified.data(), weldedMesh.indices.data(), weldedMesh.indices.size(),
        &weldedMesh.positions[0].x, weldedMesh.positions.size(),
        sizeof(Math::float4), targetIndexCount, lod_target_error(lodIndex), 0,
        &lodError);
    outStats.lodError = lodError;

    size_t finalIndexCount = simplifiedIndexCount;
    if (finalIndexCount > targetIndexCount + (targetIndexCount / 5u))
    {
        float sloppyError = 0.0f;
        const size_t sloppyIndexCount = meshopt_simplifySloppy(
            simplified.data(), weldedMesh.indices.data(),
            weldedMesh.indices.size(), &weldedMesh.positions[0].x,
            weldedMesh.positions.size(), sizeof(Math::float4), targetIndexCount,
            1.0f, &sloppyError);
        if (sloppyIndexCount >= 3u && sloppyIndexCount < finalIndexCount)
        {
            finalIndexCount = sloppyIndexCount;
            outStats.lodError = sloppyError;
            outStats.usedSloppy = true;
        }
    }

    if (finalIndexCount < 3 || finalIndexCount >= baseMesh.indices.size() ||
        finalIndexCount > targetIndexCount + (targetIndexCount / 5u))
    {
        return lodMesh;
    }

    simplified.resize(finalIndexCount);
    lodMesh = std::move(weldedMesh);
    lodMesh.indices = std::move(simplified);
    lodMesh.name = baseMesh.name + "_lod" + std::to_string(lodIndex);
    return lodMesh;
}

void log_mesh_lod_result(std::string_view meshName, uint32_t lodIndex,
                         size_t baseIndexCount, size_t targetIndexCount,
                         size_t resultIndexCount, bool accepted,
                         const LodGenerationStats *stats)
{
    const double baseTriangleCount = static_cast<double>(baseIndexCount) / 3.0;
    const double resultTriangleCount =
        static_cast<double>(resultIndexCount) / 3.0;
    const double remainingRatio = baseIndexCount > 0
                                      ? static_cast<double>(resultIndexCount) /
                                            static_cast<double>(baseIndexCount)
                                      : 0.0;
    const double reductionRatio = 1.0 - remainingRatio;

    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "[ModelImporter][LOD] mesh='{}' lod={} baseIndices={} "
                  "baseTriangles={:.0f} targetIndices={} resultIndices={} "
                  "resultTriangles={:.0f} remaining={:.2f}% reduction={:.2f}% "
                  "sourceVertices={} weldedVertices={} error={:.6f} method={} "
                  "accepted={}",
                  meshName, lodIndex, baseIndexCount, baseTriangleCount,
                  targetIndexCount, resultIndexCount, resultTriangleCount,
                  remainingRatio * 100.0, reductionRatio * 100.0,
                  stats != nullptr ? stats->sourceVertexCount : 0u,
                  stats != nullptr ? stats->weldedVertexCount : 0u,
                  stats != nullptr ? stats->lodError : 0.0f,
                  stats != nullptr && stats->usedSloppy ? "sloppy" : "regular",
                  accepted ? "true" : "false");
}

void append_render_parts_from_node(
    const aiScene &scene, const aiNode &node,
    const Math::float4x4 &parentTransform,
    const std::unordered_map<std::string, uint32_t> &jointIndices,
    Core::Native::ModelData &outModelData)
{
    const Math::float4x4 localTransform =
        convert_node_transform(node.mTransformation);
    const Math::float4x4 worldTransform = localTransform * parentTransform;
    const std::string currentNodeName = node_name(node);
    const auto jointIt = jointIndices.find(currentNodeName);
    const uint32_t jointIndex = jointIt != jointIndices.end()
                                    ? jointIt->second
                                    : Core::Native::k_invalidAnimationIndex;

    for (uint32_t nodeMeshIndex = 0; nodeMeshIndex < node.mNumMeshes;
         ++nodeMeshIndex)
    {
        const uint32_t meshIndex = node.mMeshes[nodeMeshIndex];
        if (meshIndex >= outModelData.meshes.size() ||
            meshIndex >= scene.mNumMeshes ||
            scene.mMeshes[meshIndex] == nullptr)
        {
            continue;
        }

        const aiMesh &sourceMesh = *scene.mMeshes[meshIndex];
        Core::Native::ModelRenderPartData renderPart{};
        renderPart.name = make_render_part_name(
            node, outModelData.meshes[meshIndex],
            static_cast<uint32_t>(outModelData.renderParts.size()));
        renderPart.meshIndex = meshIndex;
        renderPart.jointIndex = jointIndex;
        renderPart.localTransform = worldTransform;
        if (sourceMesh.mMaterialIndex < outModelData.materials.size())
        {
            renderPart.materialIndex = sourceMesh.mMaterialIndex;
        }
        outModelData.renderParts.push_back(std::move(renderPart));
    }

    for (uint32_t childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
    {
        if (node.mChildren[childIndex] == nullptr)
        {
            continue;
        }

        append_render_parts_from_node(scene, *node.mChildren[childIndex],
                                      worldTransform, jointIndices,
                                      outModelData);
    }
}
} // namespace

Result ModelImporter::import_model(
    const Core::IO::Path &filePath, std::string_view modelName,
    Core::Native::ModelData &outModelData) noexcept
{
    return import_model(filePath, modelName, LodGroupSettings{}, outModelData);
}

Result ModelImporter::import_model(
    const Core::IO::Path &filePath, std::string_view modelName,
    const LodGroupSettings &lodGroupSettings,
    Core::Native::ModelData &outModelData) noexcept
{
    outModelData = {};
    Core::IO::log(
        Core::IO::LogSink::console | Core::IO::LogSink::file,
        "[ModelImporter][LODGroup] model='{}' group='{}' lod1={:.2f}% "
        "lod2={:.2f}% lod3={:.2f}% billboard={}",
        modelName, lodGroupSettings.name,
        lodGroupSettings.indexRatios[0] * 100.0f,
        lodGroupSettings.indexRatios[1] * 100.0f,
        lodGroupSettings.indexRatios[2] * 100.0f,
        lodGroupSettings.generateBillboardLod ? "true" : "false");

    Assimp::Importer importer{};
    const aiScene *scene = importer.ReadFile(
        filePath.utf8(),
        aiProcess_Triangulate |          // 三角形化
            aiProcess_FlipUVs |          // UV座標の上下反転
            aiProcess_FlipWindingOrder | // 頂点の順序を反転（右手系から左手系へ変換）
            aiProcess_JoinIdenticalVertices | // 同一頂点の結合
            aiProcess_SortByPType |           // プリミティブタイプでソート
            aiProcess_ImproveCacheLocality |  // キャッシュ局所性の改善
            aiProcess_GenNormals);            // 法線の生成
    if (scene == nullptr)
    {
        return Result::fail(Code::GetFailed, Severity::Error,
                            importer.GetErrorString());
    }

    if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mNumMeshes == 0 || scene->mMeshes == nullptr)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "Model scene does not contain any mesh.");
    }

    if (scene->mNumMaterials > 0 && scene->mMaterials != nullptr)
    {
        outModelData.materials.reserve(scene->mNumMaterials);
        for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials;
             ++materialIndex)
        {
            if (scene->mMaterials[materialIndex] == nullptr)
            {
                continue;
            }

            outModelData.materials.push_back(import_material(
                *scene->mMaterials[materialIndex], materialIndex));
        }
    }

    std::vector<std::vector<uint32_t>> sourceMeshLodIndices(scene->mNumMeshes);

    // メッシュ解析
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        Core::Native::MeshData meshData;
        aiMesh *mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr)
        {
            continue;
        }
        meshData.name = make_mesh_name(*mesh, modelName, meshIndex);
        // 頂点数とインデックス数を取得
        uint32_t vertexCount = mesh->mNumVertices; // 頂点数
        uint32_t indexCount =
            mesh->mNumFaces * 3; // インデックス数(三角形化されているので3倍)
        // メモリ確保
        meshData.positions.resize(vertexCount);
        meshData.uvs.resize(vertexCount);
        meshData.normals.resize(vertexCount);
        meshData.indices.resize(indexCount);
        // 頂点情報のコピー
        if (!mesh->HasNormals())
        {
            // 法線がないメッシュは非対応
            return Result::fail(Code::Unsupported, Severity::Error,
                                "Mesh has no normals. This is not supported.");
        }
        for (uint32_t vi = 0; vi < vertexCount; ++vi)
        {
            Math::float4 &dstPos = meshData.positions[vi];
            Math::float2 &dstUV = meshData.uvs[vi];
            Math::float3 &dstNormal = meshData.normals[vi];
            const aiVector3D &p = mesh->mVertices[vi];
            const aiVector3D &n = mesh->mNormals[vi];
            dstPos = {-p.x, p.y, p.z, 1.0f}; // X軸反転
            dstNormal = {-n.x, n.y, n.z};    // X軸反転
            if (mesh->HasTextureCoords(0))
            {
                const aiVector3D &t = mesh->mTextureCoords[0][vi];
                dstUV = {t.x, t.y};
            }
            else
            {
                dstUV = {0.0f, 0.0f}; // UV座標がない場合はダミー
            }
        }
        // インデックス情報のコピー
        uint32_t idx = 0;
        for (uint32_t fi = 0; fi < mesh->mNumFaces; ++fi)
        {
            const aiFace &face = mesh->mFaces[fi];
            // aiProcess_Triangulate を使っているので常に face.mNumIndices == 3
            for (uint32_t e = 0; e < 3; ++e)
            {
                if (face.mIndices[e] >= vertexCount)
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                                        "MeshData Index out of range: " +
                                            std::to_string(face.mIndices[e]));
                }
                meshData.indices[idx++] = face.mIndices[e];
            }
        }
        // チェック
        if (idx != indexCount)
        {
            return Result::fail(
                Code::InternalError, Severity::Error,
                "MeshData IndexCount mismatch: " + std::to_string(idx) +
                    " != " + std::to_string(indexCount));
        }

        Core::Native::MeshData remappedMesh = remap_mesh(meshData);
        const size_t baseIndexCount = remappedMesh.indices.size();
        Core::Native::MeshData optimizedMesh =
            optimize_mesh_for_render(remappedMesh);
        const uint32_t baseMeshIndex =
            static_cast<uint32_t>(outModelData.meshes.size());
        sourceMeshLodIndices[meshIndex].push_back(baseMeshIndex);
        outModelData.meshes.push_back(std::move(optimizedMesh));

        log_mesh_lod_result(outModelData.meshes[baseMeshIndex].name, 0u,
                            baseIndexCount, baseIndexCount, baseIndexCount,
                            true, nullptr);

        for (uint32_t lodArrayIndex = 0;
             lodArrayIndex <
             static_cast<uint32_t>(lodGroupSettings.indexRatios.size());
             ++lodArrayIndex)
        {
            const uint32_t lodIndex = lodArrayIndex + 1u;
            const float indexRatio =
                lodGroupSettings.indexRatios[lodArrayIndex];
            LodGenerationStats lodStats{};
            Core::Native::MeshData lodMesh =
                generate_lod_mesh(remappedMesh, indexRatio, lodIndex, lodStats);
            size_t targetIndexCount =
                static_cast<size_t>(static_cast<double>(baseIndexCount) *
                                    static_cast<double>(indexRatio));
            targetIndexCount = (std::max<size_t>)(3, targetIndexCount);
            targetIndexCount = (targetIndexCount / 3) * 3;
            const bool accepted = lodMesh.indices.size() < baseIndexCount;
            log_mesh_lod_result(outModelData.meshes[baseMeshIndex].name,
                                lodIndex, baseIndexCount, targetIndexCount,
                                lodMesh.indices.size(), accepted, &lodStats);
            if (lodMesh.indices.size() >= baseIndexCount)
            {
                continue;
            }

            Core::Native::MeshData optimizedLodMesh =
                optimize_mesh_for_render(lodMesh);
            const uint32_t lodMeshIndex =
                static_cast<uint32_t>(outModelData.meshes.size());
            sourceMeshLodIndices[meshIndex].push_back(lodMeshIndex);
            outModelData.meshes.push_back(std::move(optimizedLodMesh));
        }

        if (lodGroupSettings.generateBillboardLod)
        {
            Core::Native::MeshData billboardMesh =
                make_billboard_lod_mesh(remappedMesh);
            if (!billboardMesh.positions.empty() &&
                !billboardMesh.indices.empty())
            {
                log_mesh_lod_result(
                    outModelData.meshes[baseMeshIndex].name, 4u, baseIndexCount,
                    billboardMesh.indices.size(), billboardMesh.indices.size(),
                    true, nullptr);
                const uint32_t billboardMeshIndex =
                    static_cast<uint32_t>(outModelData.meshes.size());
                sourceMeshLodIndices[meshIndex].push_back(billboardMeshIndex);
                outModelData.meshes.push_back(
                    optimize_mesh_for_render(billboardMesh));
            }
        }
    }

    if (outModelData.meshes.empty())
    {
        return Result::fail(
            Code::InvalidArgument, Severity::Error,
            "Model importer did not generate any renderable mesh.");
    }

    /*std::unordered_map<std::string, uint32_t> jointIndices{};
    const bool hasBones = scene_has_bones(*scene);
    if (scene->mRootNode != nullptr &&
        (hasBones || scene_has_animations(*scene)))
    {
        collect_skeleton_nodes(
            *scene->mRootNode,
            Core::Native::k_invalidJointIndex,
            outModelData,
            jointIndices);
        if (hasBones)
        {
            collect_bones(*scene, outModelData, jointIndices);
        }
        import_animations(*scene, jointIndices, outModelData);
    }*/

    if (scene->mRootNode != nullptr)
    {
        /*append_render_parts_from_node(
            *scene,
            *scene->mRootNode,
            Math::float4x4::identity(),
            jointIndices,
            outModelData);*/
    }

    if (outModelData.renderParts.empty())
    {
        outModelData.renderParts.reserve(scene->mNumMeshes);
        for (uint32_t sourceMeshIndex = 0; sourceMeshIndex < scene->mNumMeshes;
             ++sourceMeshIndex)
        {
            if (sourceMeshLodIndices[sourceMeshIndex].empty())
            {
                continue;
            }

            const uint32_t baseMeshIndex =
                sourceMeshLodIndices[sourceMeshIndex][0];
            Core::Native::ModelRenderPartData renderPart{};
            renderPart.name = outModelData.meshes[baseMeshIndex].name;
            renderPart.meshIndex = baseMeshIndex;
            renderPart.lodMeshIndices = sourceMeshLodIndices[sourceMeshIndex];
            if (scene->mMeshes[sourceMeshIndex] != nullptr &&
                scene->mMeshes[sourceMeshIndex]->mMaterialIndex <
                    outModelData.materials.size())
            {
                renderPart.materialIndex =
                    scene->mMeshes[sourceMeshIndex]->mMaterialIndex;
            }
            outModelData.renderParts.push_back(std::move(renderPart));
        }
    }

    return Result::ok();
}
} // namespace Cue::Editor
