# IA 経由の統合 Vertex / Index Buffer + Indirect Draw パイプライン見解まとめ

## 前提

このメモは、可視オブジェクトを `GenerateVisibleObjectList.hlsl` で収集し、`StaticMeshBatching.hlsl` で同一メッシュ単位にまとめ、統合 VertexBuffer / IndexBuffer を IA 経由で参照しながら `ExecuteIndirect` で描画する構成に対する指摘をまとめたものです。

今回、頂点・インデックス取得を shader 内の `StructuredBuffer` からではなく、Input Assembler 経由に変更したことで、以前の `SV_VertexID` と `BaseVertexLocation` 周りの問題はかなり改善されています。

ただし、Indirect Draw の batch と `RenderObject` の対応付けにはまだ致命的な問題が残っています。

---

## 結論

現在の最大の問題はこれです。

```text
StartInstanceLocation に objectId を入れても、
SV_InstanceID は objectId から始まらない。
```

そのため、複数 batch になった時点で、shader 側が間違った `RenderObject`、`Transform`、`Material` を参照する可能性が高いです。

IA 経由にしたことで Vertex / Index の取得は正しい方向に寄りました。  
しかし、**どの object 情報を使って描画するか** がまだズレます。

---

## 1. IA 経由にした点は正しい

現在の `StaticMeshForward.hlsl` は、頂点入力を次のような形で受け取る構成になっています。

```hlsl
struct VsIn
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};
```

この形なら、頂点は IA が IndexBuffer / VertexBuffer から取得して shader に渡します。

この場合、`DrawIndexedInstanced` の `BaseVertexLocation` は、IndexBuffer から読んだ index に加算され、VertexBuffer の参照位置に反映されます。

つまり、以前のように shader 側で以下のように読んでいた場合の問題は解消されます。

```hlsl
g_positions[vertexId]
g_uvs[vertexId]
g_normals[vertexId]
```

IA 経由なら、頂点取得は固定機能側に任せられるため、統合 VertexBuffer / IndexBuffer との相性は良いです。

---

## 2. まだ残っている最大の問題

`StaticMeshBatching.hlsl` 側で、Indirect Command に以下のように値を入れている場合、

```hlsl
indirectCommand.startInstanceLocation = objectId;
```

shader 側で `SV_InstanceID` から object index を作ろうとしても、期待通りにはなりません。

間違った期待はこれです。

```hlsl
renderObjectIndex = instanceId;
// instanceId が objectId から始まることを期待する
```

しかし、`SV_InstanceID` は基本的に Draw ごとに `0, 1, 2, ...` と増える値です。

`StartInstanceLocation` は shader の `SV_InstanceID` をずらすための値ではありません。  
これは IA の per-instance vertex buffer fetch に使われるオフセットです。

そのため、batch の先頭 object が `objectId = 120` だったとしても、shader 側では次のようになり得ます。

```text
instanceId = 0, 1, 2, ...
renderObjectIndex = 0, 1, 2, ...
```

本来必要なのはこれです。

```hlsl
renderObjectIndex = batchStartObjectIndex + instanceId;
```

---

## 3. IndirectCommand に drawObjectStartIndex を追加するべき

現在の Indirect Command が draw 引数だけの場合、shader に「この batch は `g_renderObjects` の何番から始まるか」を渡せません。

そのため、Indirect Command に per-draw root constant 用の値を追加するべきです。

```hlsl
struct IndirectCommand
{
    uint drawObjectStartIndex;

    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};
```

`drawObjectStartIndex` が、その batch に含まれる最初の `RenderObject` の index です。

---

## 4. StaticMeshBatching 側の修正案

batching 側では、次のように command を作ります。

```hlsl
IndirectCommand indirectCommand;
indirectCommand.drawObjectStartIndex = objectId;
indirectCommand.indexCountPerInstance = meshRange.indexCount;
indirectCommand.instanceCount = instanceCount;
indirectCommand.startIndexLocation = meshRange.startIndex;
indirectCommand.baseVertexLocation = meshRange.baseVertex;
indirectCommand.startInstanceLocation = 0;

g_indirectCommands[dstIndex] = indirectCommand;
```

重要なのは、`startInstanceLocation` に `objectId` を入れないことです。

今の shader では `startInstanceLocation` を使う意味が薄いので、基本は `0` でよいです。

---

## 5. StaticMeshForward 側の修正案

VS 側では、per-draw root constant として渡された `g_drawObjectStartIndex` を使い、`SV_InstanceID` と足します。

```hlsl
cbuffer DrawObjectIndexConstants : register(b1)
{
    uint g_drawObjectStartIndex;
};

VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectIndex = g_drawObjectStartIndex + instanceId;

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    // 以降、input.position / input.texcoord / input.normal を使用する
}
```

この形にすると、CPU から単体 draw する場合も、Indirect batch で描画する場合も同じ考え方にできます。

単体 draw の場合は、

```text
g_drawObjectStartIndex = targetObjectIndex
instanceCount = 1
```

batch draw の場合は、

```text
g_drawObjectStartIndex = batchStartObjectIndex
instanceCount = batchObjectCount
```

となります。

---

## 6. 0xffffffffu による分岐は捨てた方がいい

現在の設計に次のような分岐がある場合、

```hlsl
const bool useIndexedDrawPath = g_drawObjectIndex.drawObjectIndex != 0xffffffffu;

const uint renderObjectIndex =
    useIndexedDrawPath ? g_drawObjectIndex.drawObjectIndex : instanceId;
```

これは設計として弱いです。

理由は以下です。

- 単体 draw と indirect draw で object index の決め方が変わる
- shader 内に不要な分岐が増える
- GPU Driven Rendering に寄せたときに邪魔になる
- `SV_InstanceID` の仕様誤解を隠しやすい

`g_drawObjectStartIndex + instanceId` に統一した方が明確です。

---

## 7. VisibleObjectList は meshId 順ではない

`GenerateVisibleObjectList.hlsl` では、可視 object を以下のように append している構成です。

```hlsl
g_renderObjectCount.InterlockedAdd(0, 1, dstIndex);
g_renderObjects[dstIndex] = renderObject;
```

これは可視 object を追加しているだけであり、`meshId` 順に並んでいる保証はありません。

一方で `StaticMeshBatching.hlsl` が以下のように前後の `meshId` を見て batch の先頭を判定している場合、

```hlsl
if (objectId > 0 && g_renderObjects[objectId - 1].meshId == objectInfo.meshId)
{
    return;
}
```

これは、`g_renderObjects` がすでに `meshId` 順に並んでいることを前提にしています。

しかし現在の visible list はその前提を満たしていません。

そのため、現状の batching は、

```text
たまたま同じ meshId が連続していたらまとめる
```

だけです。

本当に batch 化したいなら、以下の段階が必要です。

```text
VisibleObjectList
    ↓
meshId / PSO / material group などで sort or bucketize
    ↓
連続 range を batch 化
    ↓
ExecuteIndirect
```

最低でも、`meshId` 単位で visible object を並べ替えるか、mesh ごとの bucket に分類する必要があります。

---

## 8. IndirectCommand の stride を CPU 側と一致させる

HLSL 側の `IndirectCommand` のサイズと、CPU 側の `D3D12_COMMAND_SIGNATURE_DESC::ByteStride` は必ず一致させる必要があります。

例えば次の構造体なら、

```hlsl
struct IndirectCommand
{
    uint drawObjectStartIndex;      // 4 bytes

    uint indexCountPerInstance;     // 4 bytes
    uint instanceCount;             // 4 bytes
    uint startIndexLocation;        // 4 bytes
    int baseVertexLocation;         // 4 bytes
    uint startInstanceLocation;     // 4 bytes
};
```

合計は 24 bytes です。

この場合、CPU 側も command stride を 24 bytes に合わせる必要があります。

```cpp
commandSignatureDesc.ByteStride = 24;
```

もし CPU 側が 36 bytes など別の値になっていると、2個目以降の command 読み取り位置がズレて描画が壊れます。

---

## 9. ExecuteIndirect の Command Signature も修正が必要

`drawObjectStartIndex` を indirect command に含める場合、Command Signature 側も次のような構成にする必要があります。

```text
1. ROOT_CONSTANT
2. DRAW_INDEXED
```

つまり、Indirect Argument Buffer の 1 command 分は次の順になります。

```text
drawObjectStartIndex
D3D12_DRAW_INDEXED_ARGUMENTS
```

HLSL 側の `IndirectCommand` の並びと、CPU 側の `D3D12_INDIRECT_ARGUMENT_DESC` の並びは一致していなければいけません。

---

## 10. Counter の clear / UAV barrier / state transition が必須

この pipeline では、毎フレーム最低限以下が必要です。

```text
1. g_renderObjectCount を 0 に clear
2. g_indirectCommandCount を 0 に clear

3. GenerateVisibleObjectList を Dispatch
4. UAV barrier

5. StaticMeshBatching を Dispatch
6. UAV barrier

7. indirect command buffer / count buffer を INDIRECT_ARGUMENT 状態へ transition
8. ExecuteIndirect
```

これをしないと、以下の問題が起きます。

- 前フレームの count が残る
- compute shader の書き込み完了前に ExecuteIndirect が読む
- indirect argument buffer の resource state が不正になる
- GPU Based Validation でエラーが出る
- たまにだけ壊れる不安定な描画になる

---

## 11. Visible object buffer の overflow 対策が必要

現在のように単純に append する場合、

```hlsl
uint dstIndex = 0;
g_renderObjectCount.InterlockedAdd(0, 1, dstIndex);
g_renderObjects[dstIndex] = renderObject;
```

visible 数が buffer capacity を超えると壊れます。

最低限、最大数を shader に渡すべきです。

```hlsl
cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
    uint g_maxRenderObjectCount;
};
```

ただし、単純に以下のようにすると count だけは増えてしまいます。

```hlsl
g_renderObjectCount.InterlockedAdd(0, 1, dstIndex);

if (dstIndex >= g_maxRenderObjectCount)
{
    return;
}
```

厳密にやるなら、`InterlockedCompareExchange` などで上限を超えない append を実装するか、十分大きい buffer を確保した上で debug 時に overflow を検出するべきです。

---

## 12. Bindless texture には NonUniformResourceIndex を使うべき

pixel shader で以下のように bindless texture array を可変 index で参照している場合、

```hlsl
g_textures[material.textureId].GetDimensions(textureWidth, textureHeight);

const float3 textureColor =
    g_textures[material.textureId].Load(int3(texelCoord, 0)).rgb;
```

`material.textureId` は draw / instance / pixel によって異なる可能性があります。

そのため、基本は `NonUniformResourceIndex` を使うべきです。

```hlsl
const uint textureIndex = NonUniformResourceIndex(material.textureId);

g_textures[textureIndex].GetDimensions(textureWidth, textureHeight);

const float3 textureColor =
    g_textures[textureIndex].Load(int3(texelCoord, 0)).rgb;
```

これを入れないと、環境や最適化次第で descriptor indexing 周りの問題を踏む可能性があります。

---

## 13. normal 変換は非一様 scale で壊れる

現在、法線変換を以下のようにしている場合、

```hlsl
const float3 worldNormal =
    normalize(mul(float4(localNormal, 0.0f), transform.worldMatrix).xyz);
```

uniform scale だけなら大きな問題は出にくいです。

しかし、非一様 scale が入ると法線が歪みます。

本来は `normalMatrix` を別で持つべきです。

```hlsl
struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};
```

shader 側では次のように使います。

```hlsl
const float3 worldNormal =
    normalize(mul(float4(localNormal, 0.0f), transform.normalMatrix).xyz);
```

描画パイプライン検証段階では後回しでもよいですが、エンジン設計としてはいずれ必要になります。

---

## 14. 優先修正順

優先度は以下です。

```text
1. IndirectCommand に drawObjectStartIndex を追加する
2. Command Signature に ROOT_CONSTANT + DRAW_INDEXED を設定する
3. VS 側で renderObjectIndex = g_drawObjectStartIndex + instanceId に統一する
4. startInstanceLocation に objectId を入れるのをやめ、基本 0 にする
5. 0xffffffffu による draw path 分岐を消す
6. VisibleObjectList を meshId / PSO 単位で sort or bucketize する
7. IndirectCommand の HLSL stride と CPU 側 ByteStride を一致させる
8. counter clear / UAV barrier / resource state transition を毎フレーム確実に行う
9. visible object buffer の overflow 対策を入れる
10. bindless texture に NonUniformResourceIndex を使う
11. normalMatrix を導入する
```

---

## 最小修正方針

まず動作を安定させるための最小修正は以下です。

### IndirectCommand

```hlsl
struct IndirectCommand
{
    uint drawObjectStartIndex;

    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};
```

### Batching 側

```hlsl
IndirectCommand indirectCommand;
indirectCommand.drawObjectStartIndex = objectId;
indirectCommand.indexCountPerInstance = meshRange.indexCount;
indirectCommand.instanceCount = instanceCount;
indirectCommand.startIndexLocation = meshRange.startIndex;
indirectCommand.baseVertexLocation = meshRange.baseVertex;
indirectCommand.startInstanceLocation = 0;

g_indirectCommands[dstIndex] = indirectCommand;
```

### Forward VS 側

```hlsl
cbuffer DrawObjectIndexConstants : register(b1)
{
    uint g_drawObjectStartIndex;
};

VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectIndex = g_drawObjectStartIndex + instanceId;

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    // IA から来た input.position / input.texcoord / input.normal を使う
}
```

この修正で、少なくとも以下の形になります。

```text
Vertex / Index 取得      : IA が担当
mesh offset              : BaseVertexLocation が担当
batch の object 開始位置 : root constant が担当
batch 内 object 選択     : SV_InstanceID が担当
```

この分担が一番自然です。

---

## 最終評価

IA 経由に変更した判断は正しいです。

ただし、現在のままだと、Vertex / Index は正しく読めても、`Transform` / `Material` を引くための `RenderObject` index がズレます。

この状態で複数 mesh、複数 batch、複数 object を描画すると、以下のような症状が出ます。

```text
- 違う位置に描画される
- 違う material が使われる
- batch 数が増えると急に壊れる
- 単体 draw では動くのに indirect で壊れる
- object 数や並び順によって再現したりしなかったりする
```

本質的な修正は、`StartInstanceLocation` に object index を押し込むことではありません。

`drawObjectStartIndex` を per-draw root constant として渡し、

```hlsl
renderObjectIndex = g_drawObjectStartIndex + instanceId;
```

に統一することです。
