#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "GpuBuffer.h"
#include <TextureManager.h>

namespace Cue::GraphicsCore::DX12
{
    class IExternalTextureOwner
    {
    public:
        virtual ~IExternalTextureOwner() = default;
        virtual Result resolve_external_texture(uint32_t index, GpuTextureResource*& outTexture) = 0;
    };

    struct TextureEntry final
    {
        enum class Kind
        {
            None,
            Owned,
            External,
        };

        Kind kind = Kind::None;
        GpuTextureResource ownedTexture{};
        IExternalTextureOwner* owner = nullptr;
        uint32_t ownerIndex = 0;

        TextureEntry() = default;
        TextureEntry(const TextureEntry&) = delete;
        TextureEntry& operator=(const TextureEntry&) = delete;
        TextureEntry(TextureEntry&&) noexcept = default;
        TextureEntry& operator=(TextureEntry&&) noexcept = default;
    };

    class DX12TextureManager final : public ITextureManager
    {
    public:
        DX12TextureManager(DX12RenderDevice& renderDevice)
            : m_renderDevice(renderDevice)
        {
        }

        Result create_texture(const TextureDesc& desc, TextureHandle& outHandle) override
        {
            // 1) bufferingCount 分の実体スロットを確保し、論理 texture 名から複数実体へ解決できるようにする。
            const uint32_t textureCount = (std::max)(desc.bufferingCount, 1u);
            std::vector<TextureHandle> handles;
            handles.reserve(textureCount);
            for (uint32_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
            {
                TextureEntry entry{};
                entry.kind = TextureEntry::Kind::Owned;
                handles.push_back(m_textureRegistry.create(entry));
            }

            outHandle = handles.front();
            if (!desc.name.empty())
            {
                m_textureNameMap[Core::fnv1a64(desc.name)] = std::move(handles);
            }
            return Result::ok();
        }
        Result destroy_texture(const TextureHandle& handle) override
        {
            if (!m_textureRegistry.destroy(handle))
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture handle is not alive");
            }
            std::erase_if(m_textureNameMap, [&handle](auto& pair)
            {
                auto& handles = pair.second;
                std::erase(handles, handle);
                return handles.empty();
            });
             
            return Result::ok();
        }
        Result get_texture(ResourceNameId nameId, uint32_t textureIndex, TextureHandle& outHandle) override
        {
            const auto it = m_textureNameMap.find(nameId);
            if (it == m_textureNameMap.end())
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture not found");
            }

            const auto& handles = it->second;
            if (textureIndex >= handles.size())
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Buffered texture not found");
            }

            outHandle = handles[textureIndex];
            return Result::ok();
        }
        Result get_texture_instance_count(ResourceNameId nameId, uint32_t& outCount) override
        {
            const auto it = m_textureNameMap.find(nameId);
            if (it == m_textureNameMap.end())
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture not found");
            }

            outCount = static_cast<uint32_t>(it->second.size());
            return Result::ok();
        }

        Result import_external_texture(ResourceNameId nameId, IExternalTextureOwner& owner, uint32_t ownerIndex, TextureHandle& outHandle)
        {
            // 1) 実体所有は owner に残し、ここでは解決情報だけを登録する
            TextureEntry entry{};
            entry.kind = TextureEntry::Kind::External;
            entry.owner = &owner;
            entry.ownerIndex = ownerIndex;
            outHandle = m_textureRegistry.create(entry);
            m_textureNameMap[nameId].push_back(outHandle);
             
            return Result::ok();
        }

        Result try_get_texture(const TextureHandle& handle, GpuTextureResource*& outTexture)
        {
            // 1) ハンドルからエントリを解決し、所有形態ごとの取り出し先へ分岐する
            outTexture = nullptr;
            Result resolveResult = Result::ok();
            const bool found = m_textureRegistry.with(handle, [&outTexture, &resolveResult](TextureEntry& entry)
            {
                switch (entry.kind)
                {
                case TextureEntry::Kind::Owned:
                    outTexture = &entry.ownedTexture;
                    break;
                case TextureEntry::Kind::External:
                    if (entry.owner == nullptr)
                    {
                        resolveResult = Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "External texture owner is null");
                        return;
                    }
                    resolveResult = entry.owner->resolve_external_texture(entry.ownerIndex, outTexture);
                    break;
                default:
                    resolveResult = Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Texture entry is not initialized");
                    break;
                }
            });

            if (!found)
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture handle is not alive");
            }

            // 2) 外部解決失敗や未初期化を呼び出し側へ返し、無効参照を隠さない
            if (!resolveResult)
            {
                return resolveResult;
            }
            if (outTexture == nullptr)
            {
                return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Texture resource is not available");
            }

            return Result::ok();
        }
    private:
        DX12RenderDevice& m_renderDevice; // RenderDeviceへの参照
        Registry<TextureTag, TextureEntry> m_textureRegistry;
        std::unordered_map<ResourceNameId, std::vector<TextureHandle>> m_textureNameMap;
    };
} // namespace Cue::GraphicsCore::DX12
