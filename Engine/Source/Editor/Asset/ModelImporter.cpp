#include "ModelImporter.h"

// === C++ includes ===
#include <algorithm>
#include <array>
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
        [[nodiscard]] std::string make_mesh_name(
            const aiMesh& mesh,
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

        [[nodiscard]] std::string make_material_name(
            const aiMaterial& material,
            uint32_t materialIndex) noexcept
        {
            aiString name{};
            if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS &&
                name.length > 0 &&
                name.C_Str() != nullptr)
            {
                return std::string(name.C_Str(), name.length);
            }

            return "Material_" + std::to_string(materialIndex);
        }

        [[nodiscard]] std::string make_texture_asset_name(
            const aiString& texturePath) noexcept
        {
            if (texturePath.length == 0 || texturePath.C_Str() == nullptr)
            {
                return {};
            }

            const Core::IO::Path sourcePath(
                std::string(texturePath.C_Str(), texturePath.length));
            return Core::IO::Path::join(
                Core::IO::Path("Textures"),
                Core::IO::Path(sourcePath.stem() + ".dds")).utf8();
        }

        [[nodiscard]] bool is_embedded_texture_path(
            const aiString& texturePath) noexcept
        {
            return texturePath.length > 0 &&
                texturePath.C_Str() != nullptr &&
                texturePath.C_Str()[0] == '*';
        }

        [[nodiscard]] Core::Native::ImportedMaterialData import_material(
            const aiMaterial& material,
            uint32_t materialIndex) noexcept
        {
            Core::Native::ImportedMaterialData materialData{};
            materialData.name = make_material_name(material, materialIndex);

            aiColor4D diffuseColor{};
            if (aiGetMaterialColor(
                &material,
                AI_MATKEY_COLOR_DIFFUSE,
                &diffuseColor) == AI_SUCCESS)
            {
                materialData.color = Math::float4(
                    diffuseColor.r,
                    diffuseColor.g,
                    diffuseColor.b,
                    diffuseColor.a);
            }
            if (aiGetMaterialColor(
                &material,
                AI_MATKEY_BASE_COLOR,
                &diffuseColor) == AI_SUCCESS)
            {
                materialData.color = Math::float4(
                    diffuseColor.r,
                    diffuseColor.g,
                    diffuseColor.b,
                    diffuseColor.a);
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
                    material.GetTexture(
                        aiTextureType_BASE_COLOR,
                        0,
                        &texturePath);
            }
            if (textureResult == AI_SUCCESS &&
                !is_embedded_texture_path(texturePath))
            {
                materialData.sourceTexturePath =
                    std::string(texturePath.C_Str(), texturePath.length);
                materialData.textureName = make_texture_asset_name(texturePath);
                materialData.isTextureUsed = !materialData.textureName.empty();
            }

            return materialData;
        }

        [[nodiscard]] Math::float4x4 convert_node_transform(
            const aiMatrix4x4& transform) noexcept
        {
            aiVector3D scale{};
            aiQuaternion rotation{};
            aiVector3D translation{};
            transform.Decompose(scale, rotation, translation);

            return Math::make_affine_matrix(
                Math::float3(scale.x, scale.y, scale.z),
                Math::Quaternion(
                    rotation.x,
                    -rotation.y,
                    -rotation.z,
                    rotation.w),
                Math::float3(-translation.x, translation.y, translation.z));
        }

        [[nodiscard]] Math::float4x4 convert_inverse_bind_matrix(
            const aiMatrix4x4& offsetMatrix) noexcept
        {
            aiMatrix4x4 bindPoseMatrix = offsetMatrix;
            bindPoseMatrix.Inverse();
            return Math::float4x4::inverse(
                convert_node_transform(bindPoseMatrix));
        }

        [[nodiscard]] Math::float3 convert_translation(
            const aiVector3D& value) noexcept
        {
            return Math::float3(-value.x, value.y, value.z);
        }

        [[nodiscard]] Math::float3 convert_scale(const aiVector3D& value) noexcept
        {
            return Math::float3(value.x, value.y, value.z);
        }

        [[nodiscard]] Math::Quaternion convert_rotation(
            const aiQuaternion& value) noexcept
        {
            return Math::Quaternion(value.x, -value.y, -value.z, value.w);
        }

        [[nodiscard]] std::string ai_name(const aiString& value)
        {
            if (value.length == 0 || value.C_Str() == nullptr)
            {
                return {};
            }

            return std::string(value.C_Str(), value.length);
        }

        [[nodiscard]] std::string node_name(const aiNode& node)
        {
            return ai_name(node.mName);
        }

        [[nodiscard]] bool scene_has_bones(const aiScene& a_scene) noexcept
        {
            for (uint32_t meshIndex = 0; meshIndex < a_scene.mNumMeshes;
                ++meshIndex)
            {
                const aiMesh* mesh = a_scene.mMeshes[meshIndex];
                if (mesh != nullptr && mesh->mNumBones > 0)
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool scene_has_animations(const aiScene& a_scene) noexcept
        {
            return a_scene.mNumAnimations > 0 && a_scene.mAnimations != nullptr;
        }

        [[nodiscard]] std::string make_render_part_name(
            const aiNode& node,
            const Core::Native::MeshData& meshData,
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

        [[nodiscard]] std::vector<MeshoptVertex> pack_vertices(
            const Core::Native::MeshData& meshData)
        {
            std::vector<MeshoptVertex> vertices(meshData.positions.size());
            for (size_t vertexIndex = 0; vertexIndex < vertices.size();
                 ++vertexIndex)
            {
                const Math::float4& position = meshData.positions[vertexIndex];
                MeshoptVertex& vertex = vertices[vertexIndex];
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

        void unpack_vertices(
            const std::vector<MeshoptVertex>& vertices,
            Core::Native::MeshData& meshData)
        {
            meshData.positions.resize(vertices.size());
            meshData.uvs.resize(vertices.size());
            meshData.normals.resize(vertices.size());
            for (size_t vertexIndex = 0; vertexIndex < vertices.size();
                 ++vertexIndex)
            {
                const MeshoptVertex& vertex = vertices[vertexIndex];
                meshData.positions[vertexIndex] =
                    Math::float4(vertex.px, vertex.py, vertex.pz, vertex.pw);
                meshData.uvs[vertexIndex] = Math::float2(vertex.u, vertex.v);
                meshData.normals[vertexIndex] =
                    Math::float3(vertex.nx, vertex.ny, vertex.nz);
            }
        }

        [[nodiscard]] Core::Native::MeshData optimize_mesh(
            const Core::Native::MeshData& sourceMesh)
        {
            Core::Native::MeshData result = sourceMesh;
            if (sourceMesh.positions.empty() || sourceMesh.indices.empty())
            {
                return result;
            }

            std::vector<MeshoptVertex> sourceVertices =
                pack_vertices(sourceMesh);
            std::vector<unsigned int> remap(sourceVertices.size());
            const size_t uniqueVertexCount = meshopt_generateVertexRemap(
                remap.data(),
                sourceMesh.indices.data(),
                sourceMesh.indices.size(),
                sourceVertices.data(),
                sourceVertices.size(),
                sizeof(MeshoptVertex));

            std::vector<uint32_t> remappedIndices(sourceMesh.indices.size());
            std::vector<MeshoptVertex> remappedVertices(uniqueVertexCount);
            meshopt_remapIndexBuffer(
                remappedIndices.data(),
                sourceMesh.indices.data(),
                sourceMesh.indices.size(),
                remap.data());
            meshopt_remapVertexBuffer(
                remappedVertices.data(),
                sourceVertices.data(),
                sourceVertices.size(),
                sizeof(MeshoptVertex),
                remap.data());

            std::vector<uint32_t> vertexCacheIndices(remappedIndices.size());
            meshopt_optimizeVertexCache(
                vertexCacheIndices.data(),
                remappedIndices.data(),
                remappedIndices.size(),
                remappedVertices.size());

            std::vector<uint32_t> overdrawIndices(vertexCacheIndices.size());
            meshopt_optimizeOverdraw(
                overdrawIndices.data(),
                vertexCacheIndices.data(),
                vertexCacheIndices.size(),
                &remappedVertices[0].px,
                remappedVertices.size(),
                sizeof(MeshoptVertex),
                1.05f);

            std::vector<MeshoptVertex> fetchVertices(remappedVertices.size());
            const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
                fetchVertices.data(),
                overdrawIndices.data(),
                overdrawIndices.size(),
                remappedVertices.data(),
                remappedVertices.size(),
                sizeof(MeshoptVertex));
            fetchVertices.resize(fetchedVertexCount);

            result.indices = std::move(overdrawIndices);
            unpack_vertices(fetchVertices, result);
            return result;
        }

        [[nodiscard]] Core::Native::MeshData generate_lod_mesh(
            const Core::Native::MeshData& baseMesh,
            float indexRatio,
            uint32_t lodIndex)
        {
            Core::Native::MeshData lodMesh = baseMesh;
            if (baseMesh.indices.size() < 3 || baseMesh.positions.empty())
            {
                return lodMesh;
            }

            size_t targetIndexCount =
                static_cast<size_t>(
                    static_cast<double>(baseMesh.indices.size()) *
                    static_cast<double>(indexRatio));
            targetIndexCount = (std::max<size_t>)(3, targetIndexCount);
            targetIndexCount = (targetIndexCount / 3) * 3;

            std::vector<uint32_t> simplified(baseMesh.indices.size());
            float lodError = 0.0f;
            const size_t simplifiedIndexCount = meshopt_simplify(
                simplified.data(),
                baseMesh.indices.data(),
                baseMesh.indices.size(),
                &baseMesh.positions[0].x,
                baseMesh.positions.size(),
                sizeof(Math::float4),
                targetIndexCount,
                0.02f,
                0,
                &lodError);

            if (simplifiedIndexCount < 3 ||
                simplifiedIndexCount >= baseMesh.indices.size())
            {
                return lodMesh;
            }

            simplified.resize(simplifiedIndexCount);
            lodMesh.indices = std::move(simplified);
            lodMesh.name = baseMesh.name + "_lod" + std::to_string(lodIndex);
            lodMesh = optimize_mesh(lodMesh);
            return lodMesh;
        }

        void append_render_parts_from_node(const aiScene& scene,
            const aiNode& node,
            const Math::float4x4& parentTransform,
            const std::unordered_map<std::string, uint32_t>& jointIndices,
            Core::Native::ModelData& outModelData)
        {
            const Math::float4x4 localTransform =
                convert_node_transform(node.mTransformation);
            const Math::float4x4 worldTransform =
                localTransform * parentTransform;
            const std::string currentNodeName = node_name(node);
            const auto jointIt = jointIndices.find(currentNodeName);
            const uint32_t jointIndex =
                jointIt != jointIndices.end()
                ? jointIt->second
                : Core::Native::k_invalidAnimationIndex;

            for (uint32_t nodeMeshIndex = 0;
                nodeMeshIndex < node.mNumMeshes;
                ++nodeMeshIndex)
            {
                const uint32_t meshIndex = node.mMeshes[nodeMeshIndex];
                if (meshIndex >= outModelData.meshes.size() ||
                    meshIndex >= scene.mNumMeshes ||
                    scene.mMeshes[meshIndex] == nullptr)
                {
                    continue;
                }

                const aiMesh& sourceMesh = *scene.mMeshes[meshIndex];
                Core::Native::ModelRenderPartData renderPart{};
                renderPart.name = make_render_part_name(
                    node,
                    outModelData.meshes[meshIndex],
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

            for (uint32_t childIndex = 0; childIndex < node.mNumChildren;
                ++childIndex)
            {
                if (node.mChildren[childIndex] == nullptr)
                {
                    continue;
                }

                append_render_parts_from_node(
                    scene,
                    *node.mChildren[childIndex],
                    worldTransform,
                    jointIndices,
                    outModelData);
            }
        }
    }

    Result ModelImporter::import_model(
        const Core::IO::Path& filePath,
        std::string_view modelName,
        Core::Native::ModelData& outModelData) noexcept
    {
        outModelData = {};

        Assimp::Importer importer{};
        const aiScene* scene = importer.ReadFile(
            filePath.utf8(),
            aiProcess_Triangulate | // 三角形化
            aiProcess_FlipUVs | // UV座標の上下反転
            aiProcess_FlipWindingOrder | // 頂点の順序を反転（右手系から左手系へ変換）
            aiProcess_JoinIdenticalVertices | // 同一頂点の結合
            aiProcess_SortByPType | // プリミティブタイプでソート
            aiProcess_ImproveCacheLocality | // キャッシュ局所性の改善
            aiProcess_GenNormals); // 法線の生成
        if (scene == nullptr)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                importer.GetErrorString());
        }

        if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
            scene->mNumMeshes == 0 ||
            scene->mMeshes == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Model scene does not contain any mesh.");
        }

        if (scene->mNumMaterials > 0 && scene->mMaterials != nullptr)
        {
            outModelData.materials.reserve(scene->mNumMaterials);
            for (uint32_t materialIndex = 0;
                materialIndex < scene->mNumMaterials;
                ++materialIndex)
            {
                if (scene->mMaterials[materialIndex] == nullptr)
                {
                    continue;
                }

                outModelData.materials.push_back(import_material(
                    *scene->mMaterials[materialIndex],
                    materialIndex));
            }
        }

        std::vector<std::vector<uint32_t>> sourceMeshLodIndices(
            scene->mNumMeshes);

        // メッシュ解析
        for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            Core::Native::MeshData meshData;
            aiMesh* mesh = scene->mMeshes[meshIndex];
            if (mesh == nullptr)
            {
                continue;
            }
            meshData.name = make_mesh_name(*mesh, modelName, meshIndex);
            // 頂点数とインデックス数を取得
            uint32_t vertexCount = mesh->mNumVertices;// 頂点数
            uint32_t indexCount = mesh->mNumFaces * 3;// インデックス数(三角形化されているので3倍)
            // メモリ確保
            meshData.positions.resize(vertexCount);
            meshData.uvs.resize(vertexCount);
            meshData.normals.resize(vertexCount);
            meshData.indices.resize(indexCount);
            // 頂点情報のコピー
            if (!mesh->HasNormals())
            {
                // 法線がないメッシュは非対応
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Mesh has no normals. This is not supported.");
            }
            for (uint32_t vi = 0; vi < vertexCount; ++vi)
            {
                Math::float4& dstPos = meshData.positions[vi];
                Math::float2& dstUV = meshData.uvs[vi];
                Math::float3& dstNormal = meshData.normals[vi];
                const aiVector3D& p = mesh->mVertices[vi];
                const aiVector3D& n = mesh->mNormals[vi];
                dstPos = { -p.x, p.y, p.z, 1.0f };// X軸反転
                dstNormal = { -n.x, n.y, n.z };// X軸反転
                if (mesh->HasTextureCoords(0))
                {
                    const aiVector3D& t = mesh->mTextureCoords[0][vi];
                    dstUV = { t.x, t.y };
                }
                else
                {
                    dstUV = { 0.0f, 0.0f };// UV座標がない場合はダミー
                }
            }
            // インデックス情報のコピー
            uint32_t idx = 0;
            for (uint32_t fi = 0; fi < mesh->mNumFaces; ++fi)
            {
                const aiFace& face = mesh->mFaces[fi];
                // aiProcess_Triangulate を使っているので常に face.mNumIndices == 3
                for (uint32_t e = 0; e < 3; ++e)
                {
                    if (face.mIndices[e] >= vertexCount)
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "MeshData Index out of range: " + std::to_string(face.mIndices[e]));
                    }
                    meshData.indices[idx++] = face.mIndices[e];
                }
            }
            // チェック
            if (idx != indexCount)
            {
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "MeshData IndexCount mismatch: " + std::to_string(idx) + " != " + std::to_string(indexCount));
            }

            Core::Native::MeshData optimizedMesh = optimize_mesh(meshData);
            const uint32_t baseMeshIndex =
                static_cast<uint32_t>(outModelData.meshes.size());
            sourceMeshLodIndices[meshIndex].push_back(baseMeshIndex);
            outModelData.meshes.push_back(std::move(optimizedMesh));

            constexpr float k_lodIndexRatios[] = { 0.5f, 0.25f, 0.125f };
            for (uint32_t lodArrayIndex = 0;
                 lodArrayIndex < static_cast<uint32_t>(std::size(k_lodIndexRatios));
                 ++lodArrayIndex)
            {
                const uint32_t lodIndex = lodArrayIndex + 1u;
                Core::Native::MeshData lodMesh = generate_lod_mesh(
                    outModelData.meshes[baseMeshIndex],
                    k_lodIndexRatios[lodArrayIndex],
                    lodIndex);
                if (lodMesh.indices.size() >=
                    outModelData.meshes[baseMeshIndex].indices.size())
                {
                    continue;
                }

                const uint32_t lodMeshIndex =
                    static_cast<uint32_t>(outModelData.meshes.size());
                sourceMeshLodIndices[meshIndex].push_back(lodMeshIndex);
                outModelData.meshes.push_back(std::move(lodMesh));
            }
        }

        if (outModelData.meshes.empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
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
            for (uint32_t sourceMeshIndex = 0;
                sourceMeshIndex < scene->mNumMeshes;
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
}
