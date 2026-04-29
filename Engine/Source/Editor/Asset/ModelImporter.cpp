#include "ModelImporter.h"

// === C++ includes ===
#include <string>

// === ThirdParty includes ===
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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

        // メッシュ解析
        for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            Core::Native::MeshData meshData;
            aiMesh* mesh = scene->mMeshes[meshIndex];
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

            // メッシュデータをモデルデータに追加
            outModelData.meshes.push_back(std::move(meshData));
        }

        if (outModelData.meshes.empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Model importer did not generate any renderable mesh.");
        }

        return Result::ok();
    }
}
