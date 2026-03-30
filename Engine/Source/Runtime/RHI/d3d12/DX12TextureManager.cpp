#include "DX12TextureManager.h"

namespace Cue::RHI::DX12
{
    Result DX12TextureManager::create_texture(const TextureDesc& desc, TextureHandle& out)
    {
        desc;
        out;
        return Result();
    }
    Result DX12TextureManager::destroy_texture(TextureHandle handle)
    {
        handle;
        return Result();
    }

    bool DX12TextureManager::try_get_record(TextureHandle handle, DX12TextureRecord*& outRecord)
    {
        // 1) 参照先を初期化して、失敗時のぶら下がりポインタを防ぎます。
        outRecord = nullptr;

        // 2) テクスチャ実体は ViewManager など backend 内部だけが参照します。
        return m_textureRegistry.with(handle, [&outRecord](DX12TextureRecord& record)
            {
                outRecord = &record;
            });
    }
}
