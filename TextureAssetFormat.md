# Texture Asset Format v1

## 目的

`cuetexture` は CueEngine の Texture 用 cooked asset 形式です。  
この仕様は `Texture v1` の固定化を目的とし、Editor 側 cooker と Runtime 側 loader の契約を定義します。

## 適用範囲

- 対象 asset: `Texture`
- 対象拡張子: `.cuetexture`
- 対象フェーズ:
  - Editor での cook
  - Runtime / Release での load

## 設計方針

- Runtime は source image を直接読まない
- Runtime は `cuetexture` のみを読む
- source image の decode / mip 生成は Editor 側 cooker の責務とする
- `Material` は source 名ではなく cooked 名を参照する

## v1 の制約

- 2D texture のみ対応
- `arraySize = 1` 固定
- cubemap 非対応
- volume texture 非対応
- payload は mip 単位で保持する

## ファイルレイアウト

ファイル全体は次の順で並ぶ。

```text
[ CueTextureHeader ]
[ CueTextureMipInfo * mipCount ]
[ pixel payload ]
```

## バイナリ定義

Runtime 側の共有定義は [`TextureAssetFormat.h`](C:/Users/sinse/source/repos/CueEngine/Engine/Source/Runtime/Engine/Asset/TextureAssetFormat.h:1) にある。

```cpp
struct CueTextureHeader final
{
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t mipCount;
    uint32_t arraySize;
    uint32_t format;
    uint32_t flags;
    uint64_t dataSize;
};

struct CueTextureMipInfo final
{
    uint32_t width;
    uint32_t height;
    uint32_t rowPitch;
    uint32_t slicePitch;
    uint64_t offset;
    uint64_t size;
};
```

## ヘッダ項目

- `magic`
  - 固定値 `0x54455543`
  - ASCII では `CUET`
- `version`
  - 現在は `1`
- `width`
  - ベース mip の横幅
- `height`
  - ベース mip の縦幅
- `mipCount`
  - mip の総数
- `arraySize`
  - v1 では `1`
- `format`
  - `RHI::ColorFormat` の値を保存する
- `flags`
  - 追加属性
  - 現在は `k_cueTextureFlagSrgb = 0x1`
- `dataSize`
  - payload 全体の合計 byte 数

## mip 情報

各 `CueTextureMipInfo` は 1 mip 分の payload 情報を持つ。

- `width`
  - その mip の横幅
- `height`
  - その mip の縦幅
- `rowPitch`
  - 1 row の byte 数
- `slicePitch`
  - その mip 全体の byte 数
- `offset`
  - payload 先頭からの byte offset
- `size`
  - その mip payload の byte 数

v1 の Runtime 実装では `slicePitch == size` を前提に読む。

## format

v1 で想定する format は次の通り。

- `R8G8B8A8_UNORM`
- `R8G8B8A8_UNORM_SRGB`

将来の `BC` 系圧縮 format は v2 以降で追加する。

## 参照規則

`Material` は texture 参照として cooked 名を保持する。

例:

```json
{
    "texture": "Textures/uvChecker.cuetexture"
}
```

`TextureId` は保存しない。`TextureId` は Runtime 読み込み時に解決される実行時 ID である。

## Cook ルール

Editor 側は source image を入力として `cuetexture` を生成する。

- 入力: `Assets/Textures/*.png`
- 出力: `Assets/Textures/*.cuetexture`
- cooker: `DirectXTex`

現在の cook 処理は [`EditorManager.cpp`](C:/Users/sinse/source/repos/CueEngine/Engine/Source/Editor/Ui/EditorManager.cpp:570) にある。

## 自動再 cook

project 読み込み時に次の条件で再 cook する。

- 対応する `.cuetexture` が存在しない
- source `.png` の `mtime_ns` が `.cuetexture` より新しい

それ以外では既存の `.cuetexture` をそのまま使う。

## Runtime load

Runtime 側は `cuetexture` を読み、mip 情報を `TextureSubresourceData` 配列へ展開して GPU へ upload する。

実装箇所:

- [`AssetManager.cpp`](C:/Users/sinse/source/repos/CueEngine/Engine/Source/Runtime/Engine/asset/AssetManager.cpp:34)
- [`AssetManager.cpp`](C:/Users/sinse/source/repos/CueEngine/Engine/Source/Runtime/Engine/asset/AssetManager.cpp:483)

## Engine 共通 texture

`CueDummy` は engine 共通の fallback texture として扱う。

- source: `Engine/Textures/CueDummy.png`
- cooked: `Engine/Textures/CueDummy.cuetexture`
- `textureId = 0`

Runtime 初期化時は [`Engine.cpp`](C:/Users/sinse/source/repos/CueEngine/Engine/Source/Runtime/Engine/Engine.cpp:67) で `CueDummy.cuetexture` を登録する。

## App / Editor の読込順

- 先に `Texture`
- 次に `Material`
- 最後に `Scene`

理由:

`Material` の texture 参照を読み込み時に `textureId` へ解決するため。

## v1 で未対応の項目

- cubemap
- array texture
- 3D texture
- BC 圧縮
- texture manifest
- hash ベースの差分 cook
- platform 別 format 分岐

## 次の拡張候補

- BC1 / BC3 / BC7 の導入
- platform 別 cook
- hash ベースの再 cook 判定
- `cuetexture` の formal validator
- asset manifest への依存登録
