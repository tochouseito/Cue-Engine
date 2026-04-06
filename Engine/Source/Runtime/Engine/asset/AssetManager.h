#pragma once

// === Base Includes ===
#include <Result.h>
#include <CueAssert.h>

// === Core Includes ===
#include <Native/Handle.h>
#include <Native/EngineNativeStruct.h>
#include <Container/Registry.h>

// === RHI Includes ===
#include <StaticMeshPool.h>

// === C++ Includes ===
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Cue
{
    struct ModelTag {};
    using ModelHandle = Core::Handle<ModelTag>;

    struct ModelAssetRecord final
    {
        Core::Native::ModelData modelData{};
        std::vector<RHI::staticMeshHandle> staticMeshHandles{};
    };

    class AssetManager final
    {
    public:
        AssetManager() = default;
        ~AssetManager() = default;
        void initialize(RHI::IStaticMeshPool* a_staticMeshPool) noexcept
        {
            m_staticMeshPool = a_staticMeshPool;
        }
        Result create_cube_model(ModelHandle& outHandle);
        Result get_model(ModelHandle handle, Core::Native::ModelData& outData) const
        {
            // asset manager の registry を唯一の原本として扱い、呼び出し側にはコピーだけ返す。
            ModelAssetRecord record{};
            if (!m_modelRegistry.try_copy_get(handle, record))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Model not found for the given handle.");
            }

            outData = record.modelData;
            return Result::ok();
        }
        Result get_static_mesh_handle(ModelHandle handle, uint32_t meshIndex, RHI::staticMeshHandle& outHandle) const
        {
            ModelAssetRecord record{};
            if (!m_modelRegistry.try_copy_get(handle, record))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Model not found for the given handle.");
            }
            if (meshIndex >= record.staticMeshHandles.size())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Mesh index is out of range for the model.");
            }

            outHandle = record.staticMeshHandles[meshIndex];
            return Result::ok();
        }
    private:
        Result add_model(std::string_view name, const Core::Native::ModelData& data, ModelHandle& outHandle)
        {
            // 同名モデルの重複登録を防ぎ、呼び出し側が安定した handle を扱えるようにする。
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (m_modelNameMap.contains(nameId))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Model with the same name already exists.");
            }

            if (m_staticMeshPool == nullptr)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Static mesh pool is not initialized in AssetManager.");
            }

            // Core 側の SoA model data を原本として保持しつつ、GPU 側の静的メッシュ登録結果も対応付ける。
            ModelAssetRecord record{};
            record.modelData = data;
            record.staticMeshHandles.reserve(record.modelData.meshes.size());
            for (const Core::Native::MeshData& meshData : record.modelData.meshes)
            {
                RHI::staticMeshHandle staticMeshHandle{};
                Result result = m_staticMeshPool->allocate_mesh(meshData, staticMeshHandle);
                if (!result)
                {
                    for (RHI::staticMeshHandle allocatedHandle : record.staticMeshHandles)
                    {
                        m_staticMeshPool->free_mesh(allocatedHandle);
                    }
                    return result;
                }

                record.staticMeshHandles.push_back(staticMeshHandle);
            }

            outHandle = m_modelRegistry.create(record);
            m_modelNameMap.emplace(nameId, outHandle);
            return Result::ok();
        }
    private:
        RHI::IStaticMeshPool* m_staticMeshPool = nullptr;
        Core::Registry<ModelTag, ModelAssetRecord> m_modelRegistry;
        std::unordered_map<Core::ResourceNameId, ModelHandle> m_modelNameMap;
    };
}
