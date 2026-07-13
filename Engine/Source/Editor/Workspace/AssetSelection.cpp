#include "AssetSelection.h"

// === C++ includes ===
#include <algorithm>
#include <cctype>
#include <string>

namespace Cue::Editor
{
    AssetKind classify_asset_kind(const Core::IO::Path& a_path) noexcept
    {
        std::string extension = a_path.extension();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](const unsigned char a_character) {
                           return static_cast<char>(std::tolower(a_character));
                       });

        if (extension == ".cuescene")
        {
            return AssetKind::scene;
        }
        if (extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
            extension == ".obj" || extension == ".dae" || extension == ".3ds")
        {
            return AssetKind::model;
        }
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".tga" || extension == ".bmp" || extension == ".dds" ||
            extension == ".hdr")
        {
            return AssetKind::texture;
        }
        if (extension == ".cuemat" || extension == ".material" ||
            extension == ".mat")
        {
            return AssetKind::material;
        }
        if (extension == ".h" || extension == ".hpp" || extension == ".cpp")
        {
            return AssetKind::script;
        }

        return AssetKind::unknown;
    }

    const char* asset_kind_name(const AssetKind a_kind) noexcept
    {
        switch (a_kind)
        {
        case AssetKind::scene:
            return "Scene";
        case AssetKind::model:
            return "Model";
        case AssetKind::texture:
            return "Texture";
        case AssetKind::material:
            return "Material";
        case AssetKind::script:
            return "Script";
        case AssetKind::unknown:
        default:
            return "Unknown";
        }
    }
} // namespace Cue::Editor
