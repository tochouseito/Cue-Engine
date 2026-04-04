#include "AssetManager.h"

namespace Cue
{
    Result AssetManager::create_cube_model(ModelHandle& outHandle)
    {
        // 構造体の用意
        Core::Native::ModelData modelData{};
        Core::Native::MeshData meshData{};

        // 頂点データとインデックスデータの用意
        std::string name = "Cube";
        uint32_t verticesCount = 24;
        uint32_t indicesCount = 36;
        meshData.name = name;
        meshData.positions.resize(verticesCount);
        meshData.uvs.resize(verticesCount);
        meshData.normals.resize(verticesCount);
        meshData.indices.resize(indicesCount);

        auto set_vertex = [&meshData](uint32_t index, const Math::float4& position, const Math::float2& uv, const Math::float3& normal)
            {
                meshData.positions[index] = position;
                meshData.uvs[index] = uv;
                meshData.normals[index] = normal;
            };

        // 頂点データの設定（位置、UV、法線など）
        // 右面
        set_vertex(0, { 0.5f,  0.5f,  0.5f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }); // 右上
        set_vertex(1, { 0.5f,  0.5f, -0.5f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }); // 左上
        set_vertex(2, { 0.5f, -0.5f,  0.5f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }); // 右下
        set_vertex(3, { 0.5f, -0.5f, -0.5f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }); // 左下

        // 左面
        set_vertex(4, { -0.5f,  0.5f, -0.5f, 1.0f }, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }); // 左上
        set_vertex(5, { -0.5f,  0.5f,  0.5f, 1.0f }, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }); // 右上
        set_vertex(6, { -0.5f, -0.5f, -0.5f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }); // 左下
        set_vertex(7, { -0.5f, -0.5f,  0.5f, 1.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }); // 右下

        // 前面
        set_vertex(8, { -0.5f,  0.5f,  0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }); // 左上
        set_vertex(9, { 0.5f,  0.5f,  0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }); // 右上
        set_vertex(10, { -0.5f, -0.5f,  0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }); // 左下
        set_vertex(11, { 0.5f, -0.5f,  0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }); // 右下

        // 後面
        set_vertex(12, { 0.5f,  0.5f, -0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }); // 右上
        set_vertex(13, { -0.5f,  0.5f, -0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }); // 左上
        set_vertex(14, { 0.5f, -0.5f, -0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }); // 右下
        set_vertex(15, { -0.5f, -0.5f, -0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }); // 左下

        // 上面
        set_vertex(16, { -0.5f,  0.5f, -0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }); // 左奥
        set_vertex(17, { 0.5f,  0.5f, -0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }); // 右奥
        set_vertex(18, { -0.5f,  0.5f,  0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }); // 左前
        set_vertex(19, { 0.5f,  0.5f,  0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }); // 右前

        // 下面
        set_vertex(20, { -0.5f, -0.5f,  0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }); // 左前
        set_vertex(21, { 0.5f, -0.5f,  0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }); // 右前
        set_vertex(22, { -0.5f, -0.5f, -0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }); // 左奥
        set_vertex(23, { 0.5f, -0.5f, -0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }); // 右奥

        // インデックスデータの設定（トライアングルリスト）
        // 右面インデックス
        meshData.indices[0] = 0; meshData.indices[1] = 2; meshData.indices[2] = 1;
        meshData.indices[3] = 2; meshData.indices[4] = 3; meshData.indices[5] = 1;

        // 左面インデックス
        meshData.indices[6] = 4; meshData.indices[7] = 6; meshData.indices[8] = 5;
        meshData.indices[9] = 6; meshData.indices[10] = 7; meshData.indices[11] = 5;

        // 前面インデックス
        meshData.indices[12] = 8; meshData.indices[13] = 10; meshData.indices[14] = 9;
        meshData.indices[15] = 10; meshData.indices[16] = 11; meshData.indices[17] = 9;

        // 後面インデックス
        meshData.indices[18] = 12; meshData.indices[19] = 14; meshData.indices[20] = 13;
        meshData.indices[21] = 14; meshData.indices[22] = 15; meshData.indices[23] = 13;

        // 上面インデックス
        meshData.indices[24] = 16; meshData.indices[25] = 18; meshData.indices[26] = 17;
        meshData.indices[27] = 18; meshData.indices[28] = 19; meshData.indices[29] = 17;

        // 下面インデックス
        meshData.indices[30] = 20; meshData.indices[31] = 22; meshData.indices[32] = 21;
        meshData.indices[33] = 22; meshData.indices[34] = 23; meshData.indices[35] = 21;

        // メッシュデータをモデルデータに追加
        modelData.meshes.push_back(std::move(meshData));

        return add_model("cube", modelData, outHandle);
    }
}
