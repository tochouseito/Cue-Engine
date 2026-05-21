#include "FontAtlasManager.h"

// === Core includes ===
#include <Native/Handle.h>

// === ThirdParty includes ===
#include <ft2build.h>
#include FT_FREETYPE_H

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace Cue::DrawSystem
{
    namespace
    {
        [[nodiscard]] std::string atlas_key(
            std::string_view a_fontPath,
            uint32_t a_pixelSize)
        {
            return std::string(a_fontPath) + "#" + std::to_string(a_pixelSize);
        }

        [[nodiscard]] uint32_t sanitize_size(uint32_t a_pixelSize) noexcept
        {
            return (std::clamp)(a_pixelSize, 1u, 256u);
        }
    }

    FontAtlasManager::~FontAtlasManager()
    {
        shutdown();
    }

    Result FontAtlasManager::initialize(AssetManager* a_assetManager)
    {
        if (a_assetManager == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "FontAtlasManager requires AssetManager.");
        }

        m_assetManager = a_assetManager;
        if (m_library != nullptr)
        {
            return Result::ok();
        }

        if (FT_Init_FreeType(&m_library) != 0)
        {
            m_library = nullptr;
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to initialize FreeType.");
        }

        return Result::ok();
    }

    void FontAtlasManager::shutdown() noexcept
    {
        release_faces();
        m_atlases.clear();
        if (m_library != nullptr)
        {
            FT_Done_FreeType(m_library);
            m_library = nullptr;
        }
        m_assetManager = nullptr;
    }

    std::string FontAtlasManager::default_font_path()
    {
#ifdef CUE_PROJECT_ROOT_PATH
        return Core::IO::Path::join(
            Core::IO::Path(std::string(CUE_PROJECT_ROOT_PATH)),
            Core::IO::Path("Engine/Fonts/NotoSansJP-VariableFont_wght.ttf"))
            .normalize()
            .utf8();
#else
        return "Engine/Fonts/NotoSansJP-VariableFont_wght.ttf";
#endif
    }

    void FontAtlasManager::decode_utf8(
        std::string_view a_text,
        std::vector<uint32_t>& a_outCodepoints)
    {
        a_outCodepoints.clear();
        for (size_t index = 0; index < a_text.size();)
        {
            const uint8_t lead = static_cast<uint8_t>(a_text[index]);
            uint32_t codepoint = 0xfffdu;
            size_t count = 1;
            if ((lead & 0x80u) == 0u)
            {
                codepoint = lead;
            }
            else if ((lead & 0xe0u) == 0xc0u && index + 1u < a_text.size())
            {
                codepoint = ((lead & 0x1fu) << 6u) |
                    (static_cast<uint8_t>(a_text[index + 1u]) & 0x3fu);
                count = 2;
            }
            else if ((lead & 0xf0u) == 0xe0u && index + 2u < a_text.size())
            {
                codepoint = ((lead & 0x0fu) << 12u) |
                    ((static_cast<uint8_t>(a_text[index + 1u]) & 0x3fu) << 6u) |
                    (static_cast<uint8_t>(a_text[index + 2u]) & 0x3fu);
                count = 3;
            }
            else if ((lead & 0xf8u) == 0xf0u && index + 3u < a_text.size())
            {
                codepoint = ((lead & 0x07u) << 18u) |
                    ((static_cast<uint8_t>(a_text[index + 1u]) & 0x3fu) << 12u) |
                    ((static_cast<uint8_t>(a_text[index + 2u]) & 0x3fu) << 6u) |
                    (static_cast<uint8_t>(a_text[index + 3u]) & 0x3fu);
                count = 4;
            }

            a_outCodepoints.push_back(codepoint);
            index += count;
        }
    }

    Result FontAtlasManager::ensure_text(
        std::string_view a_fontPath,
        uint32_t a_pixelSize,
        std::string_view a_text)
    {
        Atlas* atlas = nullptr;
        Result result = ensure_atlas(a_fontPath, a_pixelSize, atlas);
        if (!result)
        {
            return result;
        }

        std::vector<uint32_t> codepoints{};
        decode_utf8(a_text, codepoints);
        for (uint32_t codepoint : codepoints)
        {
            if (codepoint == '\r' || codepoint == '\n')
            {
                continue;
            }

            if (!atlas->glyphs.contains(codepoint))
            {
                result = add_glyph(*atlas, codepoint);
                if (!result)
                {
                    return result;
                }
            }
        }

        return atlas->isDirty ? upload_atlas(*atlas) : Result::ok();
    }

    const FontGlyph* FontAtlasManager::find_glyph(
        std::string_view a_fontPath,
        uint32_t a_pixelSize,
        uint32_t a_codepoint) const noexcept
    {
        const std::string key = atlas_key(
            a_fontPath.empty() ? default_font_path() : a_fontPath,
            sanitize_size(a_pixelSize));
        const auto atlasIt = m_atlases.find(key);
        if (atlasIt == m_atlases.end())
        {
            return nullptr;
        }

        const auto glyphIt = atlasIt->second.glyphs.find(a_codepoint);
        return glyphIt != atlasIt->second.glyphs.end()
            ? &glyphIt->second
            : nullptr;
    }

    FontAtlasView FontAtlasManager::atlas_view(
        std::string_view a_fontPath,
        uint32_t a_pixelSize) const noexcept
    {
        const std::string key = atlas_key(
            a_fontPath.empty() ? default_font_path() : a_fontPath,
            sanitize_size(a_pixelSize));
        const auto atlasIt = m_atlases.find(key);
        if (atlasIt == m_atlases.end())
        {
            return {};
        }

        FontAtlasView view{};
        view.textureId = atlasIt->second.textureId;
        view.ascender = atlasIt->second.ascender;
        view.lineHeight = atlasIt->second.lineHeight;
        return view;
    }

    Result FontAtlasManager::ensure_atlas(
        std::string_view a_fontPath,
        uint32_t a_pixelSize,
        Atlas*& a_outAtlas)
    {
        a_outAtlas = nullptr;
        if (m_library == nullptr || m_assetManager == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "FontAtlasManager is not initialized.");
        }

        const std::string fontPath =
            a_fontPath.empty() ? default_font_path() : std::string(a_fontPath);
        const uint32_t pixelSize = sanitize_size(a_pixelSize);
        const std::string key = atlas_key(fontPath, pixelSize);
        auto atlasIt = m_atlases.find(key);
        if (atlasIt != m_atlases.end())
        {
            a_outAtlas = &atlasIt->second;
            return Result::ok();
        }

        Atlas atlas{};
        atlas.pixelSize = pixelSize;
        atlas.textureName = "FontAtlas/" +
            std::to_string(static_cast<uint64_t>(Core::fnv1a64(key)));
        atlas.pixels.resize(
            static_cast<size_t>(atlas.width) *
            static_cast<size_t>(atlas.height) * 4u);

        if (FT_New_Face(m_library, fontPath.c_str(), 0, &atlas.face) != 0 ||
            atlas.face == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Failed to load font face.");
        }

        if (FT_Set_Pixel_Sizes(atlas.face, 0, pixelSize) != 0)
        {
            FT_Done_Face(atlas.face);
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Failed to set font pixel size.");
        }

        atlas.ascender =
            static_cast<float>(atlas.face->size->metrics.ascender) / 64.0f;
        atlas.lineHeight =
            static_cast<float>(atlas.face->size->metrics.height) / 64.0f;
        auto [insertedIt, _] = m_atlases.emplace(key, std::move(atlas));
        a_outAtlas = &insertedIt->second;
        return upload_atlas(*a_outAtlas);
    }

    Result FontAtlasManager::add_glyph(Atlas& a_atlas, uint32_t a_codepoint)
    {
        if (FT_Load_Char(a_atlas.face, a_codepoint, FT_LOAD_RENDER) != 0)
        {
            FontGlyph missing{};
            missing.advance = a_atlas.lineHeight * 0.5f;
            a_atlas.glyphs.emplace(a_codepoint, missing);
            return Result::ok();
        }

        const FT_GlyphSlot slot = a_atlas.face->glyph;
        const FT_Bitmap& bitmap = slot->bitmap;
        const uint32_t glyphWidth = bitmap.width;
        const uint32_t glyphHeight = bitmap.rows;
        constexpr uint32_t k_padding = 1u;

        uint32_t atlasX = 0;
        uint32_t atlasY = 0;
        if (glyphWidth > 0u && glyphHeight > 0u)
        {
            if (glyphWidth + k_padding * 2u > a_atlas.width)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Glyph is wider than the font atlas.");
            }
            if (a_atlas.penX + glyphWidth + k_padding > a_atlas.width)
            {
                a_atlas.penX = k_padding;
                a_atlas.penY += a_atlas.rowHeight + k_padding;
                a_atlas.rowHeight = 0u;
            }
            if (a_atlas.penY + glyphHeight + k_padding > a_atlas.height)
            {
                return Result::fail(
                    Code::OutOfMemory,
                    Severity::Error,
                    "Font atlas is full.");
            }

            atlasX = a_atlas.penX;
            atlasY = a_atlas.penY;
            for (uint32_t y = 0; y < glyphHeight; ++y)
            {
                const auto* src =
                    bitmap.buffer + static_cast<ptrdiff_t>(y) * bitmap.pitch;
                for (uint32_t x = 0; x < glyphWidth; ++x)
                {
                    const size_t dstIndex =
                        (static_cast<size_t>(atlasY + y) * a_atlas.width +
                            static_cast<size_t>(atlasX + x)) * 4u;
                    const std::byte alpha =
                        static_cast<std::byte>(src[x]);
                    a_atlas.pixels[dstIndex + 0u] = std::byte{ 255u };
                    a_atlas.pixels[dstIndex + 1u] = std::byte{ 255u };
                    a_atlas.pixels[dstIndex + 2u] = std::byte{ 255u };
                    a_atlas.pixels[dstIndex + 3u] = alpha;
                }
            }

            a_atlas.penX += glyphWidth + k_padding;
            a_atlas.rowHeight = (std::max)(a_atlas.rowHeight, glyphHeight);
        }

        FontGlyph glyph{};
        glyph.size = Math::float2(
            static_cast<float>(glyphWidth),
            static_cast<float>(glyphHeight));
        glyph.bearing = Math::float2(
            static_cast<float>(slot->bitmap_left),
            static_cast<float>(slot->bitmap_top));
        glyph.advance = static_cast<float>(slot->advance.x) / 64.0f;
        glyph.uvRect = Math::float4(
            static_cast<float>(atlasX) / static_cast<float>(a_atlas.width),
            static_cast<float>(atlasY) / static_cast<float>(a_atlas.height),
            static_cast<float>(glyphWidth) / static_cast<float>(a_atlas.width),
            static_cast<float>(glyphHeight) / static_cast<float>(a_atlas.height));

        a_atlas.glyphs.emplace(a_codepoint, glyph);
        a_atlas.isDirty = true;
        return Result::ok();
    }

    Result FontAtlasManager::upload_atlas(Atlas& a_atlas)
    {
        if (m_assetManager == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "FontAtlasManager requires AssetManager.");
        }

        Result result = m_assetManager->register_texture_from_rgba8(
            a_atlas.textureName,
            a_atlas.width,
            a_atlas.height,
            std::span<const std::byte>(a_atlas.pixels.data(), a_atlas.pixels.size()),
            a_atlas.textureId);
        if (result)
        {
            a_atlas.isDirty = false;
        }
        return result;
    }

    void FontAtlasManager::release_faces() noexcept
    {
        for (auto& [_, atlas] : m_atlases)
        {
            if (atlas.face != nullptr)
            {
                FT_Done_Face(atlas.face);
                atlas.face = nullptr;
            }
        }
    }
}
