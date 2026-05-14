#pragma once

// === Base Includes ===
#include <Result.h>
#include <CueAssert.h>

// === Core Includes ===
#include <Native/Handle.h>
#include <Native/EngineNativeStruct.h>
#include <Container/Registry.h>
#include <IO/IFileSystem.h>

// === Math Includes ===
#include <CueMath.h>

// === DrawSystem Includes ===
#include <DrawSystem/StaticMeshPoolTypes.h>

// === RHI Includes ===
#include <TextureManager.h>

// === Engine Includes ===
#include "ModelAssetFormat.h"
#include "TextureAssetFormat.h"

// === C++ Includes ===
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Cue
{
    struct ModelTag {};
    using ModelHandle = Core::Handle<ModelTag>;

    struct MaterialTag {};
    using MaterialHandle = Core::Handle<MaterialTag>;

    struct MaterialDesc final
    {
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        std::string textureName{};
        uint32_t textureId = 0;
        float shininess = 32.0f;
        bool isTextureUsed = false;
        bool usesReflectionSkybox = false;
    };

    struct ModelAssetRecord final
    {
        std::string name{};
        Core::Native::ModelData modelData{};
        std::vector<RHI::StaticMeshHandle> staticMeshHandles{};
    };

    struct MaterialAssetRecord final
    {
        std::string name{};
        MaterialDesc desc{};
    };

    struct TextureAssetRecord final
    {
        std::string name{};
        RHI::TextureHandle textureHandle{};
    };

    class AssetManager final
    {
    public:
        static constexpr uint32_t k_materialAssetVersion = 4;
        static constexpr uint32_t k_errorTextureId = 0;

        AssetManager() = default;
        ~AssetManager() = default;
        void initialize(DrawSystem::IStaticMeshPool* a_staticMeshPool,
            RHI::ITextureManager* a_textureManager) noexcept
        {
            m_staticMeshPool = a_staticMeshPool;
            m_textureManager = a_textureManager;
        }
        Result create_cube_model(ModelHandle& outHandle);
        Result register_model(
            std::string_view name,
            const Core::Native::ModelData& data,
            ModelHandle& outHandle)
        {
            return add_model(name, data, outHandle);
        }
        Result load_model_from_obj(
            Core::IO::IFileSystem& fileSystem,
            std::string_view name,
            const Core::IO::Path& filePath,
            ModelHandle& outHandle);
        Result register_model_from_cuemodel(
            Core::IO::IFileSystem& fileSystem,
            std::string_view name,
            const Core::IO::Path& filePath,
            ModelHandle& outHandle);
        Result create_material(std::string_view name, const MaterialDesc& desc,
            MaterialHandle& outHandle);
        Result update_material(MaterialHandle handle, const MaterialDesc& desc)
        {
            MaterialAssetRecord* record = m_materialRegistry.ref_get(handle);
            if (record == nullptr)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Material not found for the given handle.");
            }

            record->desc = desc;
            return Result::ok();
        }
        Result create_color_material(std::string_view name,
            const Math::float4& color, MaterialHandle& outHandle);
        Result save_material(MaterialHandle handle,
            Core::IO::IFileSystem& fileSystem,
            const Core::IO::Path& filePath) const;
        Result load_material(Core::IO::IFileSystem& fileSystem,
            const Core::IO::Path& filePath, MaterialHandle& outHandle);
        Result register_texture_from_cuetexture(
            Core::IO::IFileSystem& fileSystem,
            std::string_view name,
            const Core::IO::Path& filePath,
            uint32_t& outTextureId);
        Result register_texture_from_file(
            Core::IO::IFileSystem& fileSystem,
            std::string_view name,
            const Core::IO::Path& filePath,
            uint32_t& outTextureId);
        Result register_error_texture_from_cuetexture(
            Core::IO::IFileSystem& fileSystem,
            const Core::IO::Path& filePath);
        Result register_error_texture_from_file(
            Core::IO::IFileSystem& fileSystem,
            const Core::IO::Path& filePath);
        Result get_texture_id(std::string_view name, uint32_t& outTextureId) const
        {
            if (name.empty())
            {
                outTextureId = k_errorTextureId;
                return Result::ok();
            }

            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (!m_textureNameMap.contains(nameId))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Texture not found for the given name.");
            }

            outTextureId = m_textureNameMap.at(nameId);
            return Result::ok();
        }
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
        Result get_model(std::string_view name, ModelHandle& outHandle) const
        {
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (!m_modelNameMap.contains(nameId))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Model not found for the given name.");
            }

            outHandle = m_modelNameMap.at(nameId);
            return Result::ok();
        }
        Result get_model_name(ModelHandle handle, std::string& outName) const
        {
            ModelAssetRecord record{};
            if (!m_modelRegistry.try_copy_get(handle, record))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Model not found for the given handle.");
            }

            outName = record.name;
            return Result::ok();
        }
        Result get_model_name_from_mesh_id(
            uint32_t meshId,
            std::string& outName) const;
        Result resolve_model_mesh_id(
            std::string_view name,
            uint32_t& outMeshId) const;
        void collect_model_names(std::vector<std::string>& outNames) const;
        Result get_static_mesh_handle(ModelHandle handle, uint32_t meshIndex, RHI::StaticMeshHandle& outHandle) const
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
        Result get_material(MaterialHandle handle, MaterialDesc& outDesc) const
        {
            MaterialAssetRecord record{};
            if (!m_materialRegistry.try_copy_get(handle, record))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Material not found for the given handle.");
            }

            outDesc = record.desc;
            return Result::ok();
        }
        Result get_material(std::string_view name, MaterialHandle& outHandle) const
        {
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (!m_materialNameMap.contains(nameId))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Material not found for the given name.");
            }

            outHandle = m_materialNameMap.at(nameId);
            return Result::ok();
        }
        Result get_material_name(MaterialHandle handle, std::string& outName) const
        {
            MaterialAssetRecord record{};
            if (!m_materialRegistry.try_copy_get(handle, record))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Material not found for the given handle.");
            }

            outName = record.name;
            return Result::ok();
        }
        Result get_texture_handle(uint32_t textureId,
            RHI::TextureHandle& outHandle) const
        {
            if (textureId >= m_textures.size())
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Texture id is out of range.");
            }

            outHandle = m_textures[textureId].textureHandle;
            return Result::ok();
        }
    private:
        Result add_model(std::string_view name, const Core::Native::ModelData& data, ModelHandle& outHandle)
        {
            // 同名モデルの重複登録を防ぎ、呼び出し側が安定した handle を扱えるようにする。
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (m_modelNameMap.contains(nameId))
            {
                outHandle = m_modelNameMap.at(nameId);
                return Result::ok();
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
            record.name = std::string(name);
            record.modelData = data;
            record.staticMeshHandles.reserve(record.modelData.meshes.size());
            for (const Core::Native::MeshData& meshData : record.modelData.meshes)
            {
                RHI::StaticMeshHandle staticMeshHandle{};
                Result result = m_staticMeshPool->allocate_mesh(meshData, staticMeshHandle);
                if (!result)
                {
                    for (RHI::StaticMeshHandle allocatedHandle : record.staticMeshHandles)
                    {
                        m_staticMeshPool->free_mesh(allocatedHandle);
                    }
                    return result;
                }

                record.staticMeshHandles.push_back(staticMeshHandle);
            }

            outHandle = m_modelRegistry.create(record);
            m_modelHandles.push_back(outHandle);
            m_modelNameMap.emplace(nameId, outHandle);
            return Result::ok();
        }
        Result add_material(std::string_view name, const MaterialDesc& desc,
            MaterialHandle& outHandle)
        {
            const Core::ResourceNameId nameId = Core::fnv1a64(name);
            if (m_materialNameMap.contains(nameId))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Material with the same name already exists.");
            }

            MaterialAssetRecord record{};
            record.name = std::string(name);
            record.desc = desc;
            outHandle = m_materialRegistry.create(record);
            m_materialNameMap.emplace(nameId, outHandle);
            return Result::ok();
        }
    private:
        DrawSystem::IStaticMeshPool* m_staticMeshPool = nullptr;
        RHI::ITextureManager* m_textureManager = nullptr;
        Core::Registry<ModelTag, ModelAssetRecord> m_modelRegistry;
        std::vector<ModelHandle> m_modelHandles{};
        std::unordered_map<Core::ResourceNameId, ModelHandle> m_modelNameMap;
        Core::Registry<MaterialTag, MaterialAssetRecord> m_materialRegistry;
        std::unordered_map<Core::ResourceNameId, MaterialHandle> m_materialNameMap;
        std::vector<TextureAssetRecord> m_textures{};
        std::unordered_map<Core::ResourceNameId, uint32_t> m_textureNameMap;
    };
}
