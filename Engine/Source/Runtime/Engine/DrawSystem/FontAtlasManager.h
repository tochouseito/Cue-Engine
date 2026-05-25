#pragma once

// === Engine includes ===
#include <Asset/AssetManager.h>

// === Core includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct FT_FaceRec_;
using FT_Face = FT_FaceRec_*;
using FT_Library = struct FT_LibraryRec_*;

namespace Cue::DrawSystem
{
    struct FontGlyph final
    {
        Math::float4 uvRect = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        Math::float2 size = Math::float2(0.0f, 0.0f);
        Math::float2 bearing = Math::float2(0.0f, 0.0f);
        float advance = 0.0f;
    };

    struct FontAtlasView final
    {
        uint32_t textureId = AssetManager::k_errorTextureId;
        float ascender = 0.0f;
        float lineHeight = 0.0f;
    };

    class FontAtlasManager final
    {
    public:
        FontAtlasManager() = default;
        FontAtlasManager(const FontAtlasManager&) = delete;
        FontAtlasManager& operator=(const FontAtlasManager&) = delete;
        ~FontAtlasManager();

        [[nodiscard]] Result initialize(AssetManager* a_assetManager);
        void shutdown() noexcept;

        [[nodiscard]] Result ensure_text(
            std::string_view a_fontPath,
            uint32_t a_pixelSize,
            std::string_view a_text);
        [[nodiscard]] const FontGlyph* find_glyph(
            std::string_view a_fontPath,
            uint32_t a_pixelSize,
            uint32_t a_codepoint) const noexcept;
        [[nodiscard]] FontAtlasView atlas_view(
            std::string_view a_fontPath,
            uint32_t a_pixelSize) const noexcept;

        [[nodiscard]] static std::string default_font_path();
        static void decode_utf8(
            std::string_view a_text,
            std::vector<uint32_t>& a_outCodepoints);

    private:
        struct Atlas final
        {
            FT_Face face = nullptr;
            std::string textureName{};
            std::vector<std::byte> pixels{};
            std::unordered_map<uint32_t, FontGlyph> glyphs{};
            uint32_t textureId = AssetManager::k_errorTextureId;
            uint32_t width = 2048;
            uint32_t height = 2048;
            uint32_t penX = 1;
            uint32_t penY = 1;
            uint32_t rowHeight = 0;
            uint32_t pixelSize = 0;
            float ascender = 0.0f;
            float lineHeight = 0.0f;
            bool isDirty = false;
        };

        [[nodiscard]] Result ensure_atlas(
            std::string_view a_fontPath,
            uint32_t a_pixelSize,
            Atlas*& a_outAtlas);
        [[nodiscard]] Result add_glyph(Atlas& a_atlas, uint32_t a_codepoint);
        [[nodiscard]] Result upload_atlas(Atlas& a_atlas);
        void release_faces() noexcept;

        AssetManager* m_assetManager = nullptr;
        FT_Library m_library = nullptr;
        std::unordered_map<std::string, Atlas> m_atlases{};
    };
}
