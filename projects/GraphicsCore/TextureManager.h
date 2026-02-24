#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    struct TextureDesc
    {

    };

    class TextureManager
    {
    public:
        TextureManager() = default;
        ~TextureManager() = default;

        TextureHandle create_texture(const TextureDesc& desc)
        {
            // 1) 現状MVPでは空レコードを登録してハンドルだけ発行する。
            (void)desc;
            return m_textureRegistry.create(TextureRecord{});
        }

        const TextureRecord* get_texture(TextureHandle handle) const noexcept
        {
            // 1) ハンドルが有効なら参照先を返し、無効なら nullptr を返す。
            const TextureRecord* record = nullptr;
            (void)m_textureRegistry.with(
                handle,
                [&record](const TextureRecord& value)
                {
                    record = &value;
                });

            return record;
        }
    private:
        TextureRegistry m_textureRegistry;
    };
} // namespace Cue::GraphicsCore
