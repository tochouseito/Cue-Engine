#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "GpuBuffer.h"
#include <TextureManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12TextureManager final : public ITextureManager
    {
    public:
        Result create_texture(const TextureDesc& desc, TextureHandle& outHandle) override
        {
            GpuTextureResource texture{};
            outHandle = m_textureRegistry.create(texture);
            if (!desc.name.empty())
            {
                m_textureNameMap[fnv1a64(desc.name)] = outHandle;
            }
            return Result::ok();
        }
        Result destroy_texture(const TextureHandle& handle) override
        {
            if (!m_textureRegistry.destroy(handle))
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture handle is not alive");
            }
            std::erase_if(m_textureNameMap, [&handle](const auto& pair) { return pair.second == handle; });
            
            return Result::ok();
        }
        Result get_texture(ResourceNameId nameId, TextureHandle& outHandle) override
        {
            if (m_textureNameMap.contains(nameId))
            {
                outHandle = m_textureNameMap[nameId];
                return Result::ok();
            }
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture not found");
        }

        Result import_external_texture(ResourceNameId nameId, const GpuTextureResource& texture, TextureHandle& outHandle)
        {
            GpuTextureResource& importedTexture = std::move(texture);
            importedTexture.m_isExternal = true;
            outHandle = m_textureRegistry.create(importedTexture);
            m_textureNameMap[nameId] = outHandle;
            
            return Result::ok();
        }

        Result try_get_texture(const TextureHandle& handle, GpuTextureResource& outTexture) const
        {
            if (!m_textureRegistry.try_get(handle, outTexture))
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture handle is not alive");
            }
            return Result::ok();
        }
    private:
        Registry<TextureTag, GpuTextureResource> m_textureRegistry;
        std::unordered_map<ResourceNameId, TextureHandle> m_textureNameMap;
    };
} // namespace Cue::GraphicsCore::DX12
