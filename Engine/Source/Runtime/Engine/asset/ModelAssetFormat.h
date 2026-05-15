#pragma once

// === C++ Includes ===
#include <cstdint>

// === Math Includes ===
#include <CueMath.h>

namespace Cue
{
    struct CueModelHeader final
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t meshCount = 0;
        uint32_t materialCount = 0;
        uint32_t renderPartCount = 0;
        uint32_t reserved = 0;
        uint64_t dataSize = 0;
    };

    struct CueModelLegacyHeader final
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t meshCount = 0;
        uint32_t reserved = 0;
        uint64_t dataSize = 0;
    };

    struct CueModelMeshInfo final
    {
        uint32_t nameSize = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t flags = 0;
        uint64_t nameOffset = 0;
        uint64_t positionsOffset = 0;
        uint64_t uvsOffset = 0;
        uint64_t normalsOffset = 0;
        uint64_t indicesOffset = 0;
    };

    struct CueModelMaterialInfo final
    {
        uint32_t nameSize = 0;
        uint32_t textureNameSize = 0;
        uint32_t flags = 0;
        uint32_t reserved = 0;
        uint64_t nameOffset = 0;
        uint64_t textureNameOffset = 0;
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        float shininess = 32.0f;
        uint32_t padding[3] = {};
    };

    struct CueModelRenderPartInfo final
    {
        uint32_t nameSize = 0;
        uint32_t meshIndex = 0;
        uint32_t materialIndex = 0;
        uint32_t reserved = 0;
        uint64_t nameOffset = 0;
        Math::float4x4 localTransform = Math::float4x4::identity();
    };

    inline constexpr uint32_t k_cueModelMagic = 0x4d455543u;
    inline constexpr uint32_t k_cueModelVersion = 2u;
    inline constexpr uint32_t k_cueModelLegacyVersion = 1u;
    inline constexpr uint32_t k_cueModelMeshFlagHasUvs = 0x1u;
    inline constexpr uint32_t k_cueModelMeshFlagHasNormals = 0x2u;
    inline constexpr uint32_t k_cueModelMaterialFlagHasTexture = 0x1u;
    inline constexpr uint32_t k_cueModelMaterialFlagUsesReflectionSkybox = 0x2u;
}
