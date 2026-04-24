#include "AssetManager.h"

// === C++ includes ===
#include <algorithm>
#include <cstring>
#include <span>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

// === Win includes ===
#include "../../PAL/Win/ConvertUTF.h"
#include <wincodec.h>
#include <wrl/client.h>

namespace Cue
{
    namespace
    {
        using Json = nlohmann::json;
        using WicFactory = Microsoft::WRL::ComPtr<IWICImagingFactory>;
        using WicDecoder = Microsoft::WRL::ComPtr<IWICBitmapDecoder>;
        using WicFrame = Microsoft::WRL::ComPtr<IWICBitmapFrameDecode>;
        using WicConverter = Microsoft::WRL::ComPtr<IWICFormatConverter>;

        struct LoadedTextureData final
        {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<std::byte> pixels{};
        };

        [[nodiscard]] Json serialize_float4(const Math::float4& value)
        {
            return Json{
                { "x", value.x },
                { "y", value.y },
                { "z", value.z },
                { "w", value.w },
            };
        }

        void deserialize_float4(const Json& json, Math::float4& outValue)
        {
            outValue.x = json.at("x").get<float>();
            outValue.y = json.at("y").get<float>();
            outValue.z = json.at("z").get<float>();
            outValue.w = json.at("w").get<float>();
        }

        [[nodiscard]] Result load_png_rgba8(
            const Core::IO::Path& filePath,
            LoadedTextureData& outTextureData)
        {
            std::wstring widePath{};
            Result convertResult =
                PAL::Win::utf8_to_wide(filePath.normalize().utf8(), &widePath);
            if (!convertResult)
            {
                return convertResult;
            }

            WicFactory factory{};
            HRESULT hr = ::CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory));
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to create WIC imaging factory.");
            }

            WicDecoder decoder{};
            hr = factory->CreateDecoderFromFilename(
                widePath.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                &decoder);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to open texture image file.");
            }

            WicFrame frame{};
            hr = decoder->GetFrame(0, &frame);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to decode texture image frame.");
            }

            WicConverter converter{};
            hr = factory->CreateFormatConverter(&converter);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to create WIC format converter.");
            }

            hr = converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0f,
                WICBitmapPaletteTypeCustom);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to convert texture image to RGBA8.");
            }

            hr = converter->GetSize(&outTextureData.width, &outTextureData.height);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to get texture image size.");
            }

            if (outTextureData.width == 0 || outTextureData.height == 0)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture image size must be greater than zero.");
            }

            const uint32_t bytesPerPixel = 4;
            const uint64_t rowPitch =
                static_cast<uint64_t>(outTextureData.width) * bytesPerPixel;
            const uint64_t totalByteSize =
                rowPitch * static_cast<uint64_t>(outTextureData.height);
            outTextureData.pixels.resize(static_cast<size_t>(totalByteSize));
            hr = converter->CopyPixels(
                nullptr,
                static_cast<UINT>(rowPitch),
                static_cast<UINT>(totalByteSize),
                reinterpret_cast<BYTE*>(outTextureData.pixels.data()));
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to copy texture image pixels.");
            }

            return Result::ok();
        }
    }

    Result AssetManager::create_material(std::string_view name,
        const MaterialDesc& desc, MaterialHandle& outHandle)
    {
        return add_material(name, desc, outHandle);
    }

    Result AssetManager::create_color_material(std::string_view name,
        const Math::float4& color, MaterialHandle& outHandle)
    {
        MaterialDesc desc{};
        desc.color = color;
        return add_material(name, desc, outHandle);
    }

    Result AssetManager::save_material(MaterialHandle handle,
        Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath) const
    {
        MaterialAssetRecord record{};
        if (!m_materialRegistry.try_copy_get(handle, record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Material not found for the given handle.");
        }

        const Core::IO::Path normalizedPath = filePath.normalize();
        if (normalizedPath.extension() != ".cuematerial")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Material asset file extension must be .cuematerial.");
        }

        try
        {
            Json root = {
                { "version", k_materialAssetVersion },
                { "name", record.name },
                { "color", serialize_float4(record.desc.color) },
                { "textureId", record.desc.textureId },
            };

            std::string text = root.dump(4);
            text.push_back('\n');

            const std::span<const char> charSpan(text.data(), text.size());
            const std::span<const std::byte> byteSpan = std::as_bytes(charSpan);
            return fileSystem.write_all(normalizedPath, byteSpan, true);
        }
        catch (...)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Material asset could not be serialized.");
        }
    }

    Result AssetManager::load_material(Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath, MaterialHandle& outHandle)
    {
        outHandle = {};

        const Core::IO::Path normalizedPath = filePath.normalize();
        if (normalizedPath.extension() != ".cuematerial")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Material asset file extension must be .cuematerial.");
        }

        std::vector<std::byte> fileData{};
        Result result = fileSystem.read_all(normalizedPath, &fileData);
        if (!result)
        {
            return result;
        }

        try
        {
            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());
            const Json root = Json::parse(text);

            const uint32_t version = root.at("version").get<uint32_t>();
            if (version != k_materialAssetVersion)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Material asset version is not supported.");
            }

            MaterialDesc desc{};
            deserialize_float4(root.at("color"), desc.color);
            desc.textureId = root.value("textureId", k_errorTextureId);

            const std::string materialName =
                root.value("name", normalizedPath.stem());
            Result existingResult = get_material(materialName, outHandle);
            if (existingResult)
            {
                MaterialAssetRecord* record =
                    m_materialRegistry.ref_get(outHandle);
                if (record == nullptr)
                {
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "Loaded material handle could not be resolved.");
                }

                // 同名マテリアルの再読込ではハンドルを維持しつつ内容だけ更新し、
                // Scene 側が保持している参照を壊さずに色変更を反映する。
                record->name = materialName;
                record->desc = desc;
                return Result::ok();
            }
            return add_material(materialName, desc, outHandle);
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Material asset could not be parsed.");
        }
    }

    Result AssetManager::register_texture_from_png(std::string_view name,
        const Core::IO::Path& filePath,
        uint32_t& outTextureId)
    {
        outTextureId = k_errorTextureId;

        const Core::ResourceNameId nameId = Core::fnv1a64(name);
        if (m_textureNameMap.contains(nameId))
        {
            outTextureId = m_textureNameMap.at(nameId);
            return Result::ok();
        }

        if (m_textureManager == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Texture manager is not initialized in AssetManager.");
        }

        LoadedTextureData loadedTextureData{};
        Result result = load_png_rgba8(filePath, loadedTextureData);
        if (!result)
        {
            return result;
        }

        RHI::TextureDesc textureDesc{};
        textureDesc.name = std::string(name);
        textureDesc.width = loadedTextureData.width;
        textureDesc.height = loadedTextureData.height;
        textureDesc.mipLevels = 1;
        textureDesc.arraySize = 1;
        textureDesc.format = RHI::ColorFormat::R8G8B8A8_UNORM;

        const uint32_t rowPitch = loadedTextureData.width * 4u;
        RHI::TextureSubresourceData subresourceData{};
        subresourceData.data = loadedTextureData.pixels.data();
        subresourceData.dataSize = loadedTextureData.pixels.size();
        subresourceData.rowPitch = rowPitch;
        subresourceData.slicePitch =
            rowPitch * loadedTextureData.height;

        RHI::TextureHandle textureHandle{};
        result = m_textureManager->create_texture(
            textureDesc,
            std::span<const RHI::TextureSubresourceData>(&subresourceData, 1),
            textureHandle);
        if (!result)
        {
            return result;
        }

        result = m_textureManager->get_texture_descriptor_index(
            textureHandle,
            outTextureId);
        if (!result)
        {
            return result;
        }

        if (outTextureId >= m_textures.size())
        {
            m_textures.resize(static_cast<size_t>(outTextureId) + 1);
        }

        m_textures[outTextureId] = TextureAssetRecord{
            std::string(name),
            textureHandle
        };
        m_textureNameMap.emplace(nameId, outTextureId);
        return Result::ok();
    }

    Result AssetManager::register_error_texture_from_png(
        const Core::IO::Path& filePath)
    {
        if (!m_textures.empty())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Error texture must be registered before any other texture.");
        }

        uint32_t textureId = k_errorTextureId;
        Result result = register_texture_from_png("CueDummy", filePath, textureId);
        if (!result)
        {
            return result;
        }

        if (textureId != k_errorTextureId)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Error texture id must be zero.");
        }

        return Result::ok();
    }

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
