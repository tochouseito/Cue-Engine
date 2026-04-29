# Recast & Detour 機能整理と Cue Engine での役割分担

## 結論

Cue Engine では **Recast & Detour を Navigation Backend として採用**し、表側には **Cue Engine 独自の Navigation API / Component / Editor UI / Asset Pipeline** を作る。

重要なのは、Recast & Detour を「そのままエンジン機能として露出する」ことではない。  
`dtNavMesh`、`dtNavMeshQuery`、`rcConfig` などの型は Engine 内部に閉じ込め、ゲーム側・Editor 上位層には Cue Engine の型だけを見せる。

```txt
正しい方針:
    Recast & Detour = 中核アルゴリズム / Backend
    Cue Engine      = エンジン機能 / Editor統合 / ECS統合 / Asset化 / Debug表示
```

---

## Recast & Detour の構成

| モジュール | 主な役割 | Cue Engineでの使いどころ |
|---|---|---|
| `Recast` | 入力ジオメトリから NavMesh を生成 | Editor の Bake 処理 |
| `Detour` | Runtime の NavMesh 保持、経路探索、NavMesh Query | Runtime の Pathfinding |
| `DetourTileCache` | タイル NavMesh、動的障害物、部分再構築 | 後期実装。動的障害物・大きめのマップ用 |
| `DetourCrowd` | Agent の局所ステアリング、簡易回避、Crowd制御 | 最初は不要。必要になったら optional |
| `DebugUtils` | NavMesh / Query / Crowd のデバッグ描画補助 | Editor Debug View / Navigation Debug Draw |
| `RecastDemo` | サンプル・検証用アプリ | 実装参考。Cue Editor へ直接移植しない |
| `Tests` | ユニットテスト | 導入後の検証参考 |

---

# Recast & Detour にある機能

## 1. NavMesh生成

`Recast` は、入力三角形メッシュを元に NavMesh を生成する。

主な処理:

```txt
入力三角形メッシュ
    ↓
Voxel化
    ↓
歩行不可領域の除去
    ↓
Agent半径ぶんの通行領域収縮
    ↓
領域分割
    ↓
輪郭生成
    ↓
ポリゴンメッシュ生成
    ↓
Detail Mesh生成
    ↓
NavMeshデータ化
```

できること:

- Static Mesh / Collision Mesh から歩行可能領域を生成
- Agent の半径を考慮して壁際を削る
- Agent の高さを考慮して低い天井や通れない場所を除外
- 最大傾斜角で歩ける坂・歩けない坂を分類
- 最大段差で登れる段差・登れない段差を分類
- 小さい孤立領域を除去
- 領域を NavMesh ポリゴンへ変換
- Detail Mesh による高さ情報補正
- 単一 NavMesh と Tiled NavMesh の生成

Cue Engineでの担当箇所:

```txt
Editor:
    Scene Geometry収集
    ↓
    Recast Bake
    ↓
    NavMeshAsset生成
```

---

## 2. Runtime NavMesh保持

`Detour` は、Runtime で NavMesh を保持し、Query を実行するためのデータ構造を持つ。

できること:

- NavMesh データのロード
- Tile の追加・削除
- Polygon Reference による NavMesh ポリゴン管理
- Area / Flag / Cost による通行制御
- OffMesh Connection の保持
- NavMesh Query 用データの初期化

Cue Engineでは、`dtNavMesh` を直接公開せず、以下のような独自型で包む。

```cpp
struct NavMeshHandle
{
    uint32_t index = 0;
    uint32_t generation = 0;
};
```

---

## 3. 経路探索

`dtNavMeshQuery` により、NavMesh 上で経路探索と空間クエリができる。

代表的な機能:

| 機能 | 内容 |
|---|---|
| `findNearestPoly` | 指定位置に最も近い NavMesh Polygon を探す |
| `findPath` | 始点 Polygon から終点 Polygon までの Polygon 経路を探す |
| `findStraightPath` | Polygon 経路から実際に進む直線点列を作る |
| `initSlicedFindPath` / `updateSlicedFindPath` / `finalizeSlicedFindPath` | 経路探索を複数フレームに分割して実行 |
| `moveAlongSurface` | NavMesh上に拘束した移動位置を計算 |
| `raycast` | NavMesh表面上の通行可能レイを飛ばす |
| `findDistanceToWall` | 近い壁までの距離を調べる |
| `getPolyWallSegments` | Polygonの壁セグメントを取得 |
| `findLocalNeighbourhood` | 周辺の局所ポリゴン集合を取得 |
| `findPolysAroundCircle` / `findPolysAroundShape` | 範囲内のポリゴン探索 |
| `findRandomPoint` | NavMesh上のランダム点を取得 |
| `findRandomPointAroundCircle` | 指定範囲内のランダム点を取得 |
| `getPolyHeight` | Polygon上の高さを取得 |

Cue Engineで最初に公開する API は絞るべき。

```cpp
class NavigationWorld
{
public:
    Result find_nearest_point(
        NavMeshHandle navMesh,
        const Vector3& point,
        Vector3& outPoint) noexcept;

    Result find_path(
        NavMeshHandle navMesh,
        const Vector3& start,
        const Vector3& goal,
        const NavQueryFilter& filter,
        NavPath& outPath) noexcept;

    Result raycast(
        NavMeshHandle navMesh,
        const Vector3& start,
        const Vector3& end,
        const NavQueryFilter& filter,
        NavRaycastHit& outHit) noexcept;
};
```

---

## 4. Area / Flag / Cost

`Detour` は Query Filter により、通行可能・通行不可・通行コストを制御できる。

用途:

- 水場は通れるがコストを高くする
- 危険地帯を避ける
- 特定Agentだけドアを通れる
- 小型Agentだけ狭い通路を通れる
- ジャンプ可能Agentだけ OffMeshLink を使える

Cue Engine側の設計例:

```cpp
enum class NavAreaType : uint8_t
{
    Walkable,
    Mud,
    Water,
    Danger,
    Jump,
    Door,
};

struct NavQueryFilter
{
    uint16_t includeFlags = 0xffff;
    uint16_t excludeFlags = 0;
    std::array<float, 64> areaCosts{};
};
```

---

## 5. OffMesh Connection

OffMesh Connection は、通常の床接続ではない特殊な移動リンクを NavMesh に追加する機能。

用途:

- ジャンプ
- はしご
- 段差降り
- 一方通行
- ワープ
- ドア通過
- 別NavMesh領域への接続

ただし、Recast/Detour が行うのは「経路として特殊リンクを含める」まで。  
実際の移動処理、アニメーション、物理挙動は Cue Engine 側で作る。

```txt
Detour:
    OffMeshLinkをPathに含める

Cue Engine:
    JumpAnimation
    LadderMove
    Warp
    DoorOpen
    CharacterController移動
```

---

## 6. Tiled NavMesh

Tiled NavMesh は、大きなマップや再Bake・Streamingに向いた NavMesh 形式。

できること:

- NavMesh を Tile 単位で管理
- Tile の追加・削除
- 大きなマップの分割管理
- Tile 単位の再構築
- Streaming 対応の土台

Cue Engineでは最初から完全対応しなくてよいが、保存形式とAPIは将来を潰さない形にしておく。

```txt
最初:
    小規模Scene用のSingle NavMesh

後で:
    Tiled NavMesh
    Tile Streaming
    Partial Rebuild
```

---

## 7. DetourTileCache

`DetourTileCache` は、タイルNavMesh上で一時障害物や部分再構築を扱う。

できること:

- 円柱Obstacleの追加
- Box Obstacleの追加
- Y回転Box Obstacleの追加
- Obstacle削除
- 影響Tileの再構築
- 動的障害物による通行不可領域の反映

向いている用途:

- ドアが閉まって通れなくなる
- 箱を置いて通路を塞ぐ
- 一時的な障害物を置く
- 大きめのステージで部分更新したい

向いていない用途:

- 毎フレーム変形する地形
- 複雑な動的メッシュの完全NavMesh化
- 動く床そのものを歩行可能NavMeshとして扱う
- 回転・移動する足場を完全にNavMesh連動する

---

## 8. DetourCrowd

`DetourCrowd` は、複数Agentの局所移動と簡易回避を扱う。

できること:

- AgentをCrowdに登録
- 目標位置へ移動
- Agent同士の簡易衝突回避
- NavMesh上への移動拘束
- 局所ステアリング
- 速度・加速度・半径の設定
- Path Corridorの最適化

ただし制限が強い。

重要な制限:

- Agent位置の制御を Crowd Manager に渡す必要がある
- 毎フレーム外部から位置・速度を強制上書きする設計と相性が悪い
- 長距離計画より局所計画向け
- 大量Agent向けの完成品ではない
- Physics / Animation / CharacterController と自然に統合されるわけではない

Cue Engineでは最初から依存しない方がいい。

```txt
推奨:
    最初は Detour Query + Cue独自 PathFollower

後で:
    必要なら DetourCrowd を optional backend として採用
```

---

## 9. DebugUtils

`DebugUtils` は、NavMeshやQuery結果を可視化するための補助機能を持つ。

Cue Engineでは DebugUtils を直接描画するのではなく、Cue Engine の Debug Renderer に変換する。

表示すべきもの:

- NavMesh Polygon
- Tile境界
- Walkable / Not Walkable
- Area別色分け
- Path Polygon Corridor
- Straight Path
- Start / Goal
- Nearest Point
- Raycast結果
- OffMeshLink
- Obstacle
- Agent

---

# Recast & Detour にない機能 / 弱い機能

## 1. Editor UI

Recast & Detour には、Cue Engine Editor に必要な UI はない。

Cue Engineで作るもの:

- NavMesh Bake Window
- Bake Settings UI
- Agent Settings UI
- Area Settings UI
- OffMeshLink編集UI
- NavObstacle編集UI
- Scene ViewでのNavMesh表示
- Bake / Rebuild / Clear ボタン
- Bake結果のログ表示
- Bakeエラー表示

---

## 2. Scene Geometry 収集

Recastは入力三角形を受け取るだけ。  
Scene内のどのObjectをBake対象にするかは Cue Engine 側で決める。

Cue Engineで作るもの:

- Static Mesh から三角形収集
- Collider Mesh から三角形収集
- Terrain から三角形収集
- Layer / Tag / Static Flag による対象選別
- Transform適用
- World座標への変換
- 重複や無効メッシュの除外
- Bake対象Hash計算

---

## 3. Asset Pipeline

Recast & Detour は Cue Engine 用の `.cuenavmesh` を作ってくれない。

Cue Engineで作るもの:

- `NavMeshAsset`
- `.cuenavmesh`
- `NavMeshBakeSettings`
- `AgentSettings`
- `AreaSettings`
- `OffMeshLinkData`
- `NavMeshVersion`
- `SourceGeometryHash`
- `BuildHash`
- Import / Export
- Runtimeロード
- Editor再Bake判定

保存形式には、最低限これを入れる。

```txt
NavMeshAsset:
    version
    coordinateSystem
    agentSettings
    bakeSettings
    areaSettings
    sourceGeometryHash
    buildHash
    tiles
    offMeshLinks
```

---

## 4. ECS Component

Recast & Detour は ECS を知らない。  
Cue Engine側でComponentを作る。

候補:

```cpp
struct NavAgentComponent
{
    float radius = 0.3f;
    float height = 1.8f;
    float maxSpeed = 3.0f;
    float acceleration = 8.0f;
    NavAreaMask areaMask;
};

struct NavObstacleComponent
{
    float radius = 0.5f;
    float height = 1.0f;
    bool affectsNavMesh = true;
};

struct OffMeshLinkComponent
{
    EntityId start;
    EntityId end;
    bool bidirectional = true;
    NavAreaType area = NavAreaType::Jump;
};
```

---

## 5. Agent移動制御

Detour は Path を出せるが、GameObject を自然に動かしてくれるわけではない。  
DetourCrowdを使わないなら、PathFollowerは自作。

Cue Engineで作るもの:

- PathFollower
- Waypoint追従
- 旋回制御
- 加減速
- 到達判定
- Path再計算
- Stuck判定
- NavMesh上への補正
- CharacterController連携
- Rigidbody連携
- Animation連携

基本フロー:

```txt
NavigationSystem:
    find_path()
    ↓
    NavPathをNavAgentComponentへ保存
    ↓
    PathFollowerがdesiredVelocityを計算
    ↓
    CharacterController / Rigidbody / Transformへ渡す
    ↓
    必要ならNavMesh上へ補正
```

---

## 6. Physics / Collision

NavMesh は Collision Mesh ではない。

やってはいけないこと:

- NavMeshを床コリジョンとして使う
- NavMeshの高さを正確な地形高さとして信じる
- Path点列をそのままアニメーションスプラインとして使う
- NavMeshで物理衝突を解決する

Cue Engineで分離するべきもの:

```txt
Navigation:
    どこを通れるか
    どこへ向かうか

Physics:
    実際にぶつかるか
    地面に接地しているか
    押し戻し
    重力
    Rigidbody
    CharacterController
```

---

## 7. Animation

Recast & Detour は Animation を扱わない。

Cue Engineで作るもの:

- Move速度からAnimation Stateを決める
- Turn In Place
- Root Motionとの整合
- Jump Link用Animation
- Ladder Link用Animation
- Door通過Animation
- Agentの向き制御

---

## 8. 高レベルAI

Recast & Detour は AI の足回りであって、AI の脳ではない。

ないもの:

- Behavior Tree
- State Machine
- Utility AI
- GOAP
- Perception
- Cover System
- Tactical AI
- Formation
- Squad AI
- Enemy AI
- Patrol System
- Decision Making

Cue Engineでは別モジュールとして分ける。

```txt
AI:
    意思決定

Navigation:
    移動経路

Character:
    実際の移動とアニメーション
```

---

## 9. 車両・飛行・水中ナビゲーション

通常のRecast/Detourは歩行Agent向け。

弱い対象:

- 車
- バイク
- 戦車
- 船
- 飛行ユニット
- 水中ユニット
- 旋回半径が大きいAgent
- 後退や切り返しが必要なAgent

これらは別設計が必要。

例:

```txt
Vehicle Navigation:
    Lane Graph
    Spline Path
    Steering Constraint
    Turn Radius
    Speed Limit

Flying Navigation:
    3D Grid
    Voxel Navigation
    Steering / Avoidance
```

---

## 10. 大規模群衆

DetourCrowdはあるが、数百〜数千体向けの完成した群衆システムではない。

大量Agentが必要なら Cue Engine側で別設計する。

候補:

- Flow Field
- Grid Crowd
- Boids
- ECS並列更新
- Agent LOD
- GPU Crowd
- Navigation更新頻度の間引き

---

## 11. 完全な動的地形

DetourTileCacheで一時障害物は扱えるが、任意の動的地形を完全に処理できるわけではない。

弱いもの:

- 動く床
- エレベーター
- 回転足場
- 破壊される地形
- 毎フレーム変形する地面
- 複雑な動的建築物

対策:

- OffMeshLinkで処理
- 特殊移動として処理
- Local NavMeshを別管理
- NavigationではなくCharacterController側で処理
- 必要な範囲だけTileCacheで再構築

---

## 12. 安定DLL ABI / C API

Recast & Detour はソース統合向き。  
安定した C API / DLL ABI を前提にしない方がいい。

Cue Engineではこうする。

```txt
NG:
    Editor.exeやGameScript.dll側にdtNavMesh*を公開

OK:
    Engine内部だけでRecast/Detourを使う
    外部にはCue EngineのHandle/APIだけを公開
```

---

# Cue Engineでの役割分担

## 大枠

```txt
ThirdParty / RecastDetour:
    NavMesh生成アルゴリズム
    NavMesh Runtime構造
    Pathfinding
    NavMesh Query
    Tiled NavMesh
    TileCache
    Crowd optional
    DebugUtils optional

Cue Engine:
    Navigation API
    ECS Component
    Editor UI
    Asset化
    Scene Geometry収集
    Debug Renderer統合
    Agent移動
    Physics連携
    Animation連携
    AI連携
```

---

## Module構成案

```txt
CueEngine
├─ Core
│  ├─ Result
│  ├─ Handle
│  ├─ Math
│  └─ Serialization
│
├─ NavigationCore
│  ├─ NavTypes.h
│  ├─ NavMeshHandle.h
│  ├─ NavMeshAsset.h
│  ├─ NavQueryFilter.h
│  ├─ NavPath.h
│  ├─ INavMeshBackend.h
│  └─ NavigationWorld.h
│
├─ NavigationRecastBackend
│  ├─ RecastDetourBackend.h
│  ├─ RecastBakeContext.h
│  ├─ DetourNavMeshWrapper.h
│  └─ RecastDetourConverters.h
│
├─ Engine
│  ├─ NavigationSystem
│  ├─ NavAgentComponent
│  ├─ NavObstacleComponent
│  └─ OffMeshLinkComponent
│
└─ Editor
   ├─ NavMeshBakeWindow
   ├─ NavMeshDebugView
   ├─ NavMeshAssetImporter
   └─ NavigationSceneCollector
```

---

## Backend境界

Recast/Detour依存をここに閉じ込める。

```cpp
class INavMeshBackend
{
public:
    virtual ~INavMeshBackend() = default;

    virtual Result load_nav_mesh(
        const NavMeshAsset& asset,
        NavMeshHandle& outHandle) noexcept = 0;

    virtual Result unload_nav_mesh(
        NavMeshHandle handle) noexcept = 0;

    virtual Result find_nearest_point(
        NavMeshHandle handle,
        const Vector3& point,
        Vector3& outPoint) noexcept = 0;

    virtual Result find_path(
        NavMeshHandle handle,
        const Vector3& start,
        const Vector3& goal,
        const NavQueryFilter& filter,
        NavPath& outPath) noexcept = 0;

    virtual Result raycast(
        NavMeshHandle handle,
        const Vector3& start,
        const Vector3& end,
        const NavQueryFilter& filter,
        NavRaycastHit& outHit) noexcept = 0;
};
```

実装側:

```cpp
class RecastDetourBackend final : public INavMeshBackend
{
public:
    Result load_nav_mesh(
        const NavMeshAsset& asset,
        NavMeshHandle& outHandle) noexcept override;

    Result find_path(
        NavMeshHandle handle,
        const Vector3& start,
        const Vector3& goal,
        const NavQueryFilter& filter,
        NavPath& outPath) noexcept override;

private:
    // dtNavMesh / dtNavMeshQuery はここに閉じ込める
};
```

---

## Editor側の責務

Editorが担当するもの:

```txt
- Bake対象Objectの選別
- Mesh / Collider / Terrainから三角形収集
- World Transform適用
- Recast用入力データ作成
- Bake Settings編集
- Agent Settings編集
- Area Settings編集
- OffMeshLink編集
- Bake実行
- Bake結果の保存
- NavMesh Debug表示
- エラー表示
```

Editor Bake Flow:

```txt
User presses Bake
    ↓
NavigationSceneCollector collects geometry
    ↓
BakeSettings / AgentSettings を取得
    ↓
RecastDetourBackend::bake()
    ↓
NavMeshAsset を生成
    ↓
.cuenavmesh として保存
    ↓
Debug Viewに表示
```

---

## Runtime側の責務

Runtimeが担当するもの:

```txt
- NavMeshAssetロード
- NavigationWorld初期化
- NavAgentComponent更新
- Path要求受付
- PathFollower更新
- CharacterController / Rigidbody / Transform への移動指示
- Obstacle更新
- OffMeshLink通過処理
```

Runtime Flow:

```txt
Scene Load
    ↓
NavMeshAsset load
    ↓
NavigationWorldへ登録
    ↓
Agentが目的地を要求
    ↓
find_path()
    ↓
NavPath取得
    ↓
PathFollower更新
    ↓
CharacterController / Rigidbody / Transformへ反映
```

---

# 導入フェーズ

## Phase 0: 導入準備

目的: Recast/DetourをCue Engineに入れるだけ。

やること:

```txt
- ThirdParty/recastnavigation を追加
- CMake targetを分ける
- Recast/Detour型を外部公開しない方針を固定
- NavigationCoreを作る
- INavMeshBackendを作る
```

---

## Phase 1: Runtime Query MVP

目的: Bake済みNavMeshで経路探索できる状態にする。

やること:

```txt
- Detour NavMeshロード
- find_nearest_point
- find_path
- find_straight_path
- raycast
- DebugDrawでPath表示
```

この段階では Editor Bake は不要。  
まず RecastDemo などで作ったデータ、または簡易テストデータで Detour Query を動かす。

---

## Phase 2: Editor Bake MVP

目的: Cue EditorでStatic MeshからNavMeshを生成できるようにする。

やること:

```txt
- NavigationSceneCollector
- NavMeshBakeSettings
- Recast Bake実行
- .cuenavmesh保存
- NavMeshDebugView
```

最初のBake対象は Static Mesh だけでいい。  
Collider / Terrain / Layer / Area は後回し。

---

## Phase 3: Agent移動

目的: GameObjectがNavPathに沿って動く状態にする。

やること:

```txt
- NavAgentComponent
- NavigationSystem
- PathFollower
- desiredVelocity計算
- Transform移動
- 到達判定
- 再Path要求
```

最初はPhysics連携なしでいい。  
Transform直書きで動かして、あとからCharacterController/Rigidbody連携にする。

---

## Phase 4: Obstacle / OffMeshLink

目的: 実ゲームで使える特殊移動と障害物対応を追加する。

やること:

```txt
- OffMeshLinkComponent
- Jump / Ladder / Doorなどの特殊移動処理
- NavObstacleComponent
- TileCache検証
- Obstacle DebugDraw
```

---

## Phase 5: 高度化

目的: 実用度を上げる。

候補:

```txt
- Tiled NavMesh
- Tile Streaming
- DetourTileCache
- DetourCrowd optional
- Agent種類別NavMesh
- Area Cost編集
- Partial Rebuild
- Async Bake
- Navigation Debug Profiler
```

---

# やってはいけない設計

## 1. Recast/Detour型を公開APIに出す

```cpp
// NG
dtNavMesh* get_nav_mesh();
dtNavMeshQuery* get_query();
```

理由:

- Engine外部がThirdPartyに依存する
- ABI境界が弱くなる
- 将来Backend差し替え不能になる
- GameScript.dll / Editor.exe との境界が汚れる
- Recast更新時に外部コードが壊れる

---

## 2. NavMeshをCollisionとして使う

NavMeshは経路探索用の近似データ。  
物理衝突や地面判定に使うべきではない。

---

## 3. DetourCrowdを最初から中心に置く

DetourCrowdは便利だが、Agent位置制御をCrowd側に渡す必要がある。  
Cue EngineのCharacterController、Physics、Animationと衝突しやすい。

最初はこれでいい。

```txt
Detour Query
    +
Cue PathFollower
    +
Cue CharacterController
```

---

## 4. DebugDrawを後回しにする

NavMeshは見えないと調整できない。  
DebugDrawなしで実装すると、原因切り分けができない。

最低限表示するもの:

```txt
- NavMesh Polygon
- Path
- Start / Goal
- Nearest Point
- OffMeshLink
- Tile境界
```

---

## 5. Bake設定をAssetに保存しない

Bake結果だけ保存して、Bake設定を保存しないのは危険。

必ず保存する:

```txt
- agentRadius
- agentHeight
- agentMaxClimb
- agentMaxSlope
- cellSize
- cellHeight
- regionMinSize
- regionMergeSize
- edgeMaxLen
- edgeMaxError
- vertsPerPoly
- detailSampleDist
- detailSampleMaxError
- sourceGeometryHash
```

---

# 最小実装で作るべき型

```cpp
struct NavMeshHandle
{
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct NavPath
{
    std::vector<Vector3> points;
    bool partial = false;
};

struct NavRaycastHit
{
    bool hit = false;
    Vector3 position{};
    Vector3 normal{};
    float distance = 0.0f;
};

struct NavMeshBakeSettings
{
    float cellSize = 0.3f;
    float cellHeight = 0.2f;
    float agentHeight = 2.0f;
    float agentRadius = 0.6f;
    float agentMaxClimb = 0.9f;
    float agentMaxSlope = 45.0f;
    float regionMinSize = 8.0f;
    float regionMergeSize = 20.0f;
    float edgeMaxLen = 12.0f;
    float edgeMaxError = 1.3f;
    int32_t vertsPerPoly = 6;
    float detailSampleDist = 6.0f;
    float detailSampleMaxError = 1.0f;
};
```

---

# 最終的な責務表

| 項目 | Recast & Detour | Cue Engine |
|---|---:|---:|
| 三角形からNavMesh生成 | ○ | 入力収集・設定管理 |
| Voxel化 | ○ | なし |
| Walkable判定 | ○ | 設定値を渡す |
| Agent半径考慮 | ○ | Agent設定を管理 |
| NavMesh Runtime保持 | ○ | Assetロード・Handle管理 |
| 経路探索 | ○ | API化・結果変換 |
| Straight Path生成 | ○ | NavPathへ変換 |
| Area / Cost / Filter | ○ | Editor UI・独自型管理 |
| OffMesh Connection | ○ | 編集UI・移動処理 |
| Tile管理 | ○ | Asset/Scene単位の管理 |
| 一時Obstacle | △ TileCache | Component化・更新方針 |
| Crowd | △ DetourCrowd | 採用判断・Animation/Physics統合 |
| Debug描画補助 | △ DebugUtils | Debug Renderer統合 |
| Editor Bake Window | × | ○ |
| Scene Geometry収集 | × | ○ |
| `.cuenavmesh`保存 | × | ○ |
| ECS Component | × | ○ |
| CharacterController | × | ○ |
| Physics衝突 | × | ○ |
| Animation | × | ○ |
| Behavior Tree / AI判断 | × | ○ |
| Vehicle/Flying Navigation | × | 別システム |
| 大規模Crowd | ×/△ | 別システム設計 |
| 安定DLL ABI | × | 独自APIで隠蔽 |

---

# 実装方針まとめ

Cue EngineのNavigationは、次の形にする。

```txt
Recast & Detour:
    中核アルゴリズム

NavigationRecastBackend:
    Recast/Detour依存を閉じ込める層

NavigationCore:
    Cue Engine公開API

NavigationSystem:
    ECSとRuntime更新

Editor Navigation:
    Bake UI / DebugDraw / Asset保存
```

最初の目標はこれだけでいい。

```txt
1. Recast/DetourをThirdPartyに追加
2. Recast/Detour型を外部公開しない
3. NavMeshHandle / NavPath / NavQueryFilter を作る
4. Detourで find_path を動かす
5. DebugDrawでNavMeshとPathを表示する
6. EditorでStatic MeshからBakeする
7. .cuenavmesh として保存・ロードする
```

これ以上を最初から狙うと、また設計が肥大化する。  
最初は **Bake → Load → FindPath → DebugDraw → Agentが歩く** までに絞る。
