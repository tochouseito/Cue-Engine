// AssetDragDrop の役割と公開要素を定義する

#pragma once

// AssetBrowser と各 View のドラッグ操作を同じ payload 名で接続するための共通定義

namespace Cue::Editor
{
    inline constexpr char k_materialAssetPayloadType[] =
        "CueMaterialAssetPath";
    inline constexpr char k_textureAssetPayloadType[] =
        "CueTextureAssetPath";
}
