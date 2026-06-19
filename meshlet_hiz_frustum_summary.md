# MeshShaderを使わない場合のMeshlet活用方針

## 結論

MeshShaderを使わない場合、**Meshletをそのまま描画単位にするメリットは薄い**。  
特に、従来のVertex Shader / Pixel Shaderパイプラインで `1 meshlet = 1 draw` のように扱うと、Draw Command数が爆発し、GPU Driven Renderingの利点を失いやすい。

ただし、Meshletは完全に無駄ではない。  
従来パイプラインでも、以下の用途では価値がある。

- Frustum Cullingの細分化
- Hi-Z Occlusion Cullingの細分化
- 巨大メッシュの完全不可視判定
- LOD / Cluster LOD用の補助データ
- 将来的なMeshShader対応のための前処理データ
- meshoptimizerによる頂点キャッシュ・Overdraw・Index最適化

重要なのは、**Meshletを描画単位にするのではなく、判定単位として使うこと**である。

---

## MeshShaderなしで避けるべき設計

```text
1 meshlet = 1 indirect draw
```

この設計は危険である。

理由は以下の通り。

- Meshlet数が多いほどDraw Commandが大量発生する
- ExecuteIndirect用のCommand Bufferが膨らむ
- Command生成・Compaction・Prefix Sum・Merge処理が重くなる
- カリングで削った分を、間接描画管理コストで食い潰す
- 従来パイプラインではMeshShaderのようにMeshlet単位の処理を自然に吸収できない

特に、DragonやBuddhaのような高ポリゴンモデルを大量配置する場合、Meshlet単位描画は逆効果になりやすい。

---

## Meshlet単位でHi-Z / Frustum判定だけするとは

Meshlet単位でHi-Z / Frustum判定だけするとは、以下のような考え方である。

```text
Meshletを直接描画しない
↓
Meshletの境界情報だけを使う
↓
Compute Shaderで細かく可視判定する
↓
結果をObject単位の描画可否やRange生成に使う
```

つまり、Meshletは**描画単位**ではなく、**カリング判定単位**として扱う。

---

## なぜObject単位だけでは不十分な場合があるのか

Object単位のFrustum / Hi-Z Cullingでは、巨大メッシュに弱い。

例:

```text
巨大な建物
巨大な地形
巨大な橋
巨大な岩
高ポリゴンのDragon
```

このようなObjectでは、Object全体のAABBやBounding Sphereが画面内に入っているだけで、Object全体がVisible扱いになる。

```text
Object Boundsが見える
↓
Object全体を描画
↓
実際には大部分が壁・地形・他Objectの裏
↓
無駄なVertex処理 / Index Fetch / Draw負荷が発生
```

Meshlet単位で判定すると、Object内部をより細かく見られる。

```text
Objectは画面内
↓
Meshlet 0 は見える
Meshlet 1 は画面外
Meshlet 2 は遮蔽されている
Meshlet 3 は見える
...
```

これにより、Object単位ではVisibleだったものを、より正確に判定できる。

---

## 使い方1: 全Meshletが不可視ならObjectごと捨てる

最初に実装するなら、この方式が現実的である。

```text
Object BoundsはVisible
↓
そのObject内のMeshletを判定
↓
1つもVisibleなMeshletがない
↓
Object Draw Commandを生成しない
```

この方式では、Meshlet単位で部分描画はしない。

```text
Meshletが1つでもVisible → Object全体を描画
全MeshletがInvisible → Object全体をSkip
```

### メリット

- 既存のObject単位GPU Driven Renderingに接続しやすい
- Draw Command数が増えにくい
- Meshlet単位描画によるCommand爆発を避けられる
- 巨大Objectが完全に隠れているケースに効く

### デメリット

- 一部だけ見えているObjectは結局全体を描く
- 部分的なVertex処理削減にはならない
- Meshlet判定コストを払ったのにObject全体を描くケースがある

### 向いているケース

```text
巨大Objectが完全に隠れる場面が多い
Object Boundsは見えているが、中身はほぼ遮蔽されている
Object単位Hi-Zでは落としきれない
```

---

## 使い方2: 可視MeshletをRangeにまとめて描画する

より高度な方式として、VisibleなMeshletをIndex Rangeにまとめて描画する方法がある。

```text
Meshlet 0: Visible
Meshlet 1: Visible
Meshlet 2: Invisible
Meshlet 3: Visible
Meshlet 4: Visible
```

この場合、以下のように連続範囲へまとめる。

```text
Draw Range A: Meshlet 0〜1
Draw Range B: Meshlet 3〜4
```

### メリット

- 部分的に隠れた巨大メッシュのVertex処理を削減できる
- Object全体描画より細かい単位で描画負荷を削れる

### デメリット

- 可視MeshletがバラバラだとDraw Rangeが増える
- Merge / Sort / Compactionが必要になる
- ExecuteIndirect Command数が増える
- 実装難度が高い
- 従来パイプラインではMeshShaderほど自然に扱えない

したがって、以下の設計は避けるべきである。

```text
1 Meshlet = 1 Draw
```

代わりに、以下のようにまとめる必要がある。

```text
複数Meshlet = 1 Draw Range
```

---

## Meshlet Groupという現実的な中間案

従来パイプラインでは、Meshletそのものは細かすぎる。  
そのため、Meshletをさらにまとめた **Meshlet Group / Cluster Group** を使う方が現実的である。

```text
Object
  ├─ MeshletGroup 0
  │   ├─ Meshlet 0
  │   ├─ Meshlet 1
  │   ├─ Meshlet 2
  │   └─ Meshlet 3
  │
  ├─ MeshletGroup 1
  │   ├─ Meshlet 4
  │   ├─ Meshlet 5
  │   ├─ Meshlet 6
  │   └─ Meshlet 7
```

例:

```text
1 Group = 16〜64 Meshlets
```

処理の流れ:

```text
Group Boundsを判定
↓
Groupが不可視なら中のMeshletをすべてSkip
↓
Groupが可視なら描画候補にする
↓
必要ならGroup内のMeshletをさらに判定
```

従来パイプラインでは、**Meshlet単位よりMeshlet Group単位の方が描画単位として扱いやすい**。

---

## 基本パイプライン

Meshlet単位のHi-Z / Frustum判定を使う場合の基本的な流れは以下。

```text
1. Occluder Depth Prepass
2. Hi-Z Mip Chain生成
3. Object単位Frustum / Hi-Z Culling
4. Object単位でVisibleになった巨大ObjectだけMeshlet判定
5. Meshlet単位Frustum / Hi-Z Culling
6. 結果をObject VisibilityまたはDraw Range生成に反映
7. Main Forward / GBuffer / Depth Pass
```

重要なのは、**全Objectの全Meshletを毎フレーム判定しない**ことである。

---

## 必要なデータ

最低限、Meshletごとに以下の情報を持つ。

```cpp
struct MeshletBounds
{
    float3 localCenter;
    float  radius;

    uint   firstIndex;
    uint   indexCount;
    uint   materialId;
};
```

ObjectごとにMeshlet範囲を持つ。

```cpp
struct ObjectMeshletRange
{
    uint firstMeshlet;
    uint meshletCount;
};
```

Objectの可視状態を管理する。

```cpp
struct ObjectVisibility
{
    uint coarseVisible;
    uint refinedVisible;
};
```

---

## Frustum Culling

MeshletのBounding SphereをWorld空間に変換する。

```cpp
worldCenter = mul(objectMatrix, float4(localCenter, 1.0)).xyz;
worldRadius = localRadius * maxScale;
```

6つのFrustum Planeで判定する。

```cpp
bool visible = true;

for each frustum plane:
    dist = dot(plane.normal, worldCenter) + plane.d;

    if (dist < -worldRadius)
        visible = false;
```

Object単位で行っているSphere / AABB判定を、Meshlet単位に細かくするだけである。

---

## Hi-Z Occlusion Culling

Hi-ZはDepth BufferのMip Chainである。

```text
Mip 0: 通常のDepth Buffer
Mip 1: 1/2解像度
Mip 2: 1/4解像度
Mip 3: 1/8解像度
...
```

Meshlet BoundsをScreen Spaceに投影し、Screen Rectを求める。

```text
Meshlet Sphere / AABB
↓
Clip Space
↓
NDC
↓
Screen Rect
```

求める情報:

```text
minX, minY
maxX, maxY
nearestDepth
```

Screen Rectの大きさから参照するMip Levelを選ぶ。

```cpp
float2 rectSize = float2(maxX - minX, maxY - minY) * screenSize;
float maxSize = max(rectSize.x, rectSize.y);
uint mip = clamp(ceil(log2(maxSize)), 0, maxMip);
```

そのMipのHi-ZからDepthを取得し、Meshletの最前面Depthと比較する。

---

## 通常ZとReversed-Zの違い

### 通常Z

```text
Near = 0
Far  = 1
小さいDepthほど手前
```

Hi-Z Mipには、その範囲の **最大Depth** を格納する。

```text
meshletの最前面Depth > Hi-Zの最大Depth
↓
meshletは遮蔽されている
```

### Reversed-Z

```text
Near = 1
Far  = 0
大きいDepthほど手前
```

Hi-Z Mipには、その範囲の **最小Depth** を格納する。

```text
meshletの最前面Depth < Hi-Zの最小Depth
↓
meshletは遮蔽されている
```

CueEngineで `LessEqual` を使っているなら、通常Z寄りの設計である可能性が高い。  
その場合は、Hi-Z Mipに最大Depthを持たせる方が分かりやすい。

---

## Compute Shader疑似コード

```hlsl
[numthreads(64, 1, 1)]
void CS(uint3 tid : SV_DispatchThreadID)
{
    uint meshletInstanceId = tid.x;

    MeshletInstance mi = gMeshletInstances[meshletInstanceId];

    ObjectData obj = gObjects[mi.objectId];
    MeshletBounds bounds = gMeshletBounds[mi.meshletId];

    float3 worldCenter = mul(obj.world, float4(bounds.localCenter, 1.0)).xyz;
    float radius = bounds.radius * obj.maxScale;

    if (!SphereInFrustum(worldCenter, radius))
    {
        gMeshletVisible[meshletInstanceId] = 0;
        return;
    }

    if (SphereOccludedByHiZ(worldCenter, radius))
    {
        gMeshletVisible[meshletInstanceId] = 0;
        return;
    }

    gMeshletVisible[meshletInstanceId] = 1;

    // Object単位Skip方式なら、1つでもVisibleなMeshletがあればObjectをVisibleにする
    InterlockedOr(gObjectVisible[obj.objectId], 1);
}
```

この段階では描画しない。  
単にMeshletごとの可視フラグを作るだけである。

---

## Object Skip方式の実装フロー

```text
1. CoarseObjectCullCS
   - Object単位のFrustum / Hi-Z判定

2. BuildMeshletTestListCS
   - 巨大ObjectだけMeshlet判定対象に追加

3. MeshletCullCS
   - Meshlet単位のFrustum / Hi-Z判定
   - 1つでもVisibleならObjectVisible = true

4. BuildDrawCommandCS
   - ObjectVisibleなObjectだけDrawCommandを生成

5. ExecuteIndirect
   - 既存のObject単位描画を実行
```

この方式は、既存のObject単位GPU Driven Renderingに入れやすい。

---

## Range描画方式の実装フロー

```text
1. MeshletCullCS
   - Meshlet単位の可視判定

2. VisibleMeshletCompactionCS
   - Visible Meshlet Listを作成

3. MergeContiguousRangeCS
   - MeshId / MaterialId / LOD / Index連続性でMerge

4. BuildDrawCommandCS
   - DrawIndexedIndirect Commandを生成

5. ExecuteIndirect
   - Range単位で描画
```

ただし、この方式は複雑であり、可視Meshletが細切れになると逆に遅くなる。

---

## CueEngineでの推奨方針

CueEngineでは、最初からRange描画まで実装するべきではない。  
まずは以下の順序で進めるべきである。

### 1. Object単位Hi-Zを安定させる

```text
Object AABB / Sphere
↓
Frustum Culling
↓
Hi-Z Occlusion Culling
↓
DrawCommand生成
```

これが安定していないなら、Meshlet判定に進む価値は低い。

---

### 2. 巨大ObjectだけMeshlet判定する

全ObjectにMeshlet判定をかけてはいけない。

悪い例:

```text
全Object × 全Meshlet を毎フレーム判定
```

良い例:

```text
Object単位でVisibleになったもの
かつ
Triangle数が多い
かつ
Screen Sizeが大きい
かつ
Object Boundsが大きい
```

このようなObjectだけMeshlet判定する。

---

### 3. まずは「全Meshlet不可視ならObject Skip」

最初の実装はこれでよい。

```text
Object BoundsはVisible
↓
Meshlet単位でFrustum / Hi-Z判定
↓
全MeshletがInvisibleならObject Drawを消す
↓
1つでもVisibleならObject全体を描画
```

これは描画構造を壊さずに導入できる。

---

### 4. Range描画は再計測後に判断する

Range描画は、以下が確認できてから検討する。

```text
Object Skip方式では削減効果が足りない
Forward PassのVertex処理が支配的
Command生成コストに余裕がある
Visible Meshletが連続Rangeにまとまりやすい
```

この条件を満たさないなら、Range描画は実装コストに見合わない。

---

## 実装時の注意点

### 全Meshletを毎フレーム判定しない

```text
悪い:
全Object × 全Meshlet

良い:
Coarse Visible Object × Large Object × 必要Meshlet
```

---

### 近距離ObjectはHi-Z判定しない

近距離Objectは可視になりやすく、Hi-Zで落ちにくい。  
判定コストの割に効果が薄い。

```text
Near Object → Visible扱い
Mid / Far Object → Hi-Z判定
```

---

### Near PlaneをまたぐBoundsはVisible扱い

Near PlaneをまたぐMeshletはScreen Rect計算が壊れやすい。  
無理にOcclusion判定せず、Visible扱いにする。

---

### False Occlusionを避ける

カリングミスには2種類ある。

```text
False Visible:
本当は見えないのに描く
→ 遅くなるだけ

False Occluded:
本当は見えるのに消す
→ 画面破綻
```

許されるのはFalse Visibleだけである。  
そのため、Hi-Z判定は保守的に行う。

```text
Screen Rectを少し広げる
Depth比較を安全側に倒す
Mip Levelを粗めにする
Near Plane交差はVisible扱い
```

---

## 採用作品としての説明文例

```text
本エンジンでは、MeshShaderを使用せず、従来のDirectX 12パイプライン上でGPU Driven Renderingを実装しています。
Meshletは直接の描画単位としては使用せず、巨大メッシュに対する細粒度なFrustum / Hi-Z Occlusion判定の補助データとして利用します。
これにより、1 Meshlet = 1 DrawによるDraw Command数の爆発を避けつつ、Object単位では判定が粗くなる巨大メッシュの完全不可視判定を精密化しています。
```

---

## まとめ

MeshShaderを使わない場合、Meshletを描画単位にするのは基本的に悪手である。

```text
避ける:
1 Meshlet = 1 Draw

使う:
Meshlet = Culling / LOD / Bounds判定単位
```

CueEngineでの現実的な方針は以下。

```text
1. Object単位Frustum / Hi-Zを安定させる
2. 巨大ObjectだけMeshlet単位で追加判定する
3. 全Meshlet不可視ならObjectをSkipする
4. Range描画は計測後に判断する
5. Meshlet Groupを使って粒度を粗くする
```

最初に実装すべきなのは、**Meshlet単位描画ではなく、巨大Objectの完全不可視判定をMeshlet Boundsで精密化すること**である。
