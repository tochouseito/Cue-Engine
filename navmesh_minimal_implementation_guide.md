# 自作エンジン向けナビメッシュ最小実装ガイド

## 目的

このドキュメントは、自作ゲームエンジンで最初に作るべきナビメッシュ実装を、現実的な最小構成に絞って整理したものです。

最初の目標は、次の一点です。

> 任意の開始点と目的点を入力したら、ナビメッシュ上の滑らかな経路点列を返す。

最初から自動生成、動的更新、群衆回避、ジャンプリンクまで作ろうとすると、実装範囲が広すぎて破綻しやすくなります。

---

## 1. 最初に作るべきもの

最小構成はこれです。

> 静的な三角形ナビメッシュを読み込み、ポリゴン隣接グラフで A\* 探索し、Funnel Algorithm で滑らかな経路を出し、Agent がそれを追従する。

---

## 2. 最初のスコープ

### 対応するもの

- 静的な地形
- 地面を歩くキャラクター
- 1種類のエージェント半径
- 三角形または凸ポリゴンのナビメッシュ
- A\* による経路探索
- Funnel Algorithm による経路平滑化
- 単純な経路追従

### 対応しないもの

- 自動ナビメッシュ生成
- 動的障害物での再生成
- 他キャラとの高度な回避
- ジャンプ
- はしご
- ドアの開閉による動的リンク
- 複数サイズのエージェント
- 水中、空中、飛行 AI

最初に作るべきものは **Navigation Query System** です。  
AI 全体ではありません。

---

## 3. 推奨モジュール構成

```txt
Navigation/
  NavMeshAsset
  NavMeshBuilder
  NavMeshQuery
  PathFinder
  Funnel
  NavAgentComponent
  NavDebugRenderer
```

### 各モジュールの役割

```txt
NavMeshAsset
  歩行可能ポリゴン、頂点、隣接情報を持つデータ

NavMeshBuilder
  読み込んだ三角形から隣接情報を作る

NavMeshQuery
  点がどのポリゴンにあるか、最寄り点はどこかを調べる

PathFinder
  ポリゴン列を A* で探す

Funnel
  ポリゴン列から自然な waypoint を作る

NavAgentComponent
  実際のキャラを経路に沿って移動させる

NavDebugRenderer
  ナビメッシュ、探索結果、経路を可視化する
```

---

## 4. 悪い設計と良い設計

### 悪い設計

```cpp
EnemyAI::MoveToPlayer()
{
    // ここでナビメッシュ探索
    // ここで A*
    // ここで座標補正
    // ここで障害物回避
    // ここで移動
}
```

これはすぐ壊れます。  
AI、経路探索、移動制御が混ざっているためです。

### 良い設計

```cpp
Path path;
NavSystem::FindPath(enemyPos, playerPos, path);

EnemyAgent.SetPath(path);
EnemyAgent.Update(deltaTime);
```

役割を分けます。

```txt
AI
  どこへ行きたいかを決める

NavSystem
  どう行くかを返す

NavAgent
  実際に動く
```

---

## 5. ナビメッシュのデータ構造

最初は三角形ポリゴンで十分です。

```cpp
using NavPolyId = uint32_t;
static constexpr NavPolyId INVALID_NAV_POLY = UINT32_MAX;

struct NavVertex
{
    Vec3 position;
};

struct NavPoly
{
    uint32_t indices[3];      // 三角形の頂点インデックス
    NavPolyId neighbors[3];   // 各エッジの隣接ポリゴン。なければ INVALID_NAV_POLY
    Vec3 center;
    float cost = 1.0f;
};

struct NavMesh
{
    std::vector<NavVertex> vertices;
    std::vector<NavPoly> polys;
};
```

最初から凸 n 角形、タイル、BVH、複数エージェントタイプなどを入れる必要はありません。

---

## 6. ナビメッシュの作り方

最初は **自動生成しない** 方がいいです。

Blender などで、歩ける面だけを別メッシュとして作ります。

```txt
LevelMesh
  壁、床、装飾、コリジョン全部を含む通常メッシュ

NavMesh
  AI が歩ける床だけを持つ単純な三角形メッシュ
```

例：

```txt
level_01.gltf
level_01_navmesh.gltf
```

エンジン側では `level_01_navmesh.gltf` の頂点と三角形を読んで、`NavMeshAsset` に変換します。

これが最初の実装としては一番現実的です。

---

## 7. 隣接情報を作る

三角形 A と三角形 B が同じエッジを共有していたら、隣接しているとみなします。

```txt
Triangle A:
  v0, v1, v2

Triangle B:
  v2, v1, v3

共有エッジ:
  v1 - v2
```

---

## 8. 雑な隣接判定

最初は全探索でも動きます。

```cpp
for each polyA:
    for each edgeA:
        for each polyB:
            for each edgeB:
                if edgeA and edgeB share same two vertices:
                    polyA.neighbors[edgeA] = polyB;
```

ただし、これは `O(n^2)` なので、ポリゴン数が増えると重くなります。

---

## 9. ハッシュマップを使った隣接判定

実用寄りにするなら、エッジをキーにしてハッシュマップを使います。

```cpp
struct EdgeKey
{
    uint32_t a;
    uint32_t b;

    EdgeKey(uint32_t v0, uint32_t v1)
    {
        a = std::min(v0, v1);
        b = std::max(v0, v1);
    }
};

struct EdgeRef
{
    NavPolyId polyId;
    uint32_t edgeIndex;
};
```

擬似コード：

```cpp
std::unordered_map<EdgeKey, EdgeRef> edgeMap;

for each poly:
    for edgeIndex in 0..2:
        v0 = poly.indices[edgeIndex];
        v1 = poly.indices[(edgeIndex + 1) % 3];

        key = EdgeKey(v0, v1);

        if key already exists:
            other = edgeMap[key];

            poly.neighbors[edgeIndex] = other.polyId;
            navMesh.polys[other.polyId].neighbors[other.edgeIndex] = polyId;
        else:
            edgeMap[key] = { polyId, edgeIndex };
```

---

## 10. 頂点の完全一致問題

Blender などから出力したメッシュでは、見た目は同じ位置でも、頂点 ID が別になっていることがあります。

この場合、頂点インデックスだけで共有エッジ判定すると失敗します。

対策として、座標を量子化して比較します。

```cpp
struct QuantizedVec3
{
    int x;
    int y;
    int z;
};
```

例：0.001 単位で丸める。

```cpp
int qx = round(position.x * 1000.0f);
int qy = round(position.y * 1000.0f);
int qz = round(position.z * 1000.0f);
```

ここを雑にすると、見た目はつながっているのに AI が通れないバグが出ます。

---

## 11. 点がどのポリゴンにあるか調べる

経路探索の最初に必要なのは、次の2つです。

```txt
開始位置がどの三角形にあるか
目的地がどの三角形にあるか
```

最初は XZ 平面に投影して判定すれば十分です。

```txt
Vec3(x, y, z) -> Vec2(x, z)
```

### 2D 三角形内判定

```cpp
bool PointInTriangle2D(Vec2 p, Vec2 a, Vec2 b, Vec2 c)
{
    float d1 = Cross(b - a, p - a);
    float d2 = Cross(c - b, p - b);
    float d3 = Cross(a - c, p - c);

    bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);

    return !(hasNeg && hasPos);
}
```

### 所属ポリゴン検索

```cpp
NavPolyId FindContainingPoly(Vec3 position)
{
    Vec2 p = Vec2(position.x, position.z);

    for each poly:
        Vec2 a = XZ(vertices[poly.indices[0]].position);
        Vec2 b = XZ(vertices[poly.indices[1]].position);
        Vec2 c = XZ(vertices[poly.indices[2]].position);

        if PointInTriangle2D(p, a, b, c):
            return polyId;

    return INVALID_NAV_POLY;
}
```

最初は全探索でいいです。  
ポリゴン数が増えたら、後で空間分割を入れます。

候補：

```txt
Uniform Grid
BVH
Quadtree
AABB Tree
```

最初は `Uniform Grid` が一番簡単です。

---

## 12. 高さ判定の注意点

XZ だけで判定すると、上下に重なった床で壊れます。

例：

```txt
1階の床
2階の床
橋
トンネル
```

高低差があるなら、候補三角形の Y 距離も見ます。

```cpp
if PointInTriangleXZ(position):
    projectedY = ComputeTriangleHeightAtXZ(position)
    heightDiff = abs(position.y - projectedY)

    if heightDiff < maxSnapHeight:
        candidate
```

これを入れないと、1階にいる敵が2階のナビメッシュに吸着するようなバグが出ます。

---

## 13. A\* でポリゴン列を探す

ナビメッシュ上の A\* では、各ポリゴンをノードとして扱います。

```txt
startPoly -> ... -> goalPoly
```

### コスト

最初は、隣接ポリゴン中心間の距離で十分です。

```cpp
float Cost(NavPolyId a, NavPolyId b)
{
    return Distance(navMesh.polys[a].center, navMesh.polys[b].center);
}
```

### ヒューリスティック

現在ポリゴン中心から目的ポリゴン中心までの直線距離を使います。

```cpp
float Heuristic(NavPolyId current, NavPolyId goal)
{
    return Distance(navMesh.polys[current].center, navMesh.polys[goal].center);
}
```

A\* の出力は、座標列ではなく **ポリゴン列** です。

```txt
[12, 15, 18, 22, 23, 31]
```

このポリゴン列を Funnel Algorithm に渡して、最終的な waypoint に変換します。

---

## 14. ポータル列を作る

ポリゴン A からポリゴン B へ移動するとき、2つの三角形は共有エッジを持っています。  
この共有エッジが **ポータル** です。

```txt
A -> B -> C -> D
```

というポリゴン列なら、ポータル列はこうなります。

```txt
portal(A, B)
portal(B, C)
portal(C, D)
```

Funnel Algorithm は、このポータル列を使って、通路内でできるだけまっすぐな経路を作ります。

---

## 15. Funnel Algorithm

A\* でポリゴン中心をつなぐだけだと、経路が不自然になります。

```txt
start -> poly center -> poly center -> poly center -> goal
```

AI が無駄にジグザグします。

Funnel Algorithm を使うと、通路の範囲内で直線化できます。

```txt
start -> corner -> corner -> goal
```

最小実装では、出力は waypoint の配列にします。

```cpp
struct Path
{
    std::vector<Vec3> points;
};
```

---

## 16. NavSystem の API

最初の公開 API はこのくらいで十分です。

```cpp
class NavSystem
{
public:
    bool LoadNavMesh(const std::string& path);

    bool FindPath(
        const Vec3& start,
        const Vec3& goal,
        Path& outPath
    );

    bool SamplePosition(
        const Vec3& position,
        float maxDistance,
        Vec3& outPosition
    );

    bool IsOnNavMesh(const Vec3& position) const;
};
```

### FindPath の流れ

```cpp
bool NavSystem::FindPath(const Vec3& start, const Vec3& goal, Path& outPath)
{
    NavPolyId startPoly = FindContainingPoly(start);
    NavPolyId goalPoly  = FindContainingPoly(goal);

    if (startPoly == INVALID_NAV_POLY || goalPoly == INVALID_NAV_POLY)
        return false;

    std::vector<NavPolyId> polyPath;
    if (!AStar(startPoly, goalPoly, polyPath))
        return false;

    std::vector<Portal> portals;
    BuildPortals(polyPath, portals);

    Funnel(start, goal, portals, outPath.points);

    return true;
}
```

---

## 17. SamplePosition

現実には、Agent の位置やクリック位置がナビメッシュ上にないことがあります。

そのため、`FindContainingPoly` だけでは足りません。

必要なのはこれです。

```cpp
bool SamplePosition(
    const Vec3& position,
    float maxDistance,
    Vec3& outPosition
);
```

これは、指定座標の近くにあるナビメッシュ上の点を探す関数です。

最初は全三角形に対して最近点を探せば十分です。

```cpp
for each triangle:
    closest = ClosestPointOnTriangle(position, triangle)
    distance = Distance(position, closest)

    if distance < bestDistance:
        best = closest
```

---

## 18. より現実的な FindPath

`SamplePosition` を含めると、最小構成の `FindPath` はこうなります。

```cpp
bool NavSystem::FindPath(Vec3 start, Vec3 goal, Path& outPath)
{
    Vec3 snappedStart;
    Vec3 snappedGoal;

    if (!SamplePosition(start, 1.0f, snappedStart))
        return false;

    if (!SamplePosition(goal, 1.0f, snappedGoal))
        return false;

    NavPolyId startPoly = FindContainingPoly(snappedStart);
    NavPolyId goalPoly  = FindContainingPoly(snappedGoal);

    if (startPoly == INVALID_NAV_POLY || goalPoly == INVALID_NAV_POLY)
        return false;

    std::vector<NavPolyId> polyPath;

    if (!FindPolyPathAStar(startPoly, goalPoly, polyPath))
        return false;

    std::vector<Portal> portals;

    if (!BuildPortalPath(polyPath, portals))
        return false;

    outPath.points.clear();

    RunFunnel(snappedStart, snappedGoal, portals, outPath.points);

    return !outPath.points.empty();
}
```

---

## 19. NavAgentComponent

NavAgent は Path を追従するだけにします。

```cpp
class NavAgentComponent
{
public:
    void SetDestination(const Vec3& destination);
    void SetPath(const Path& path);
    void Update(float deltaTime);

private:
    Path currentPath;
    int currentWaypoint = 0;

    float speed = 3.0f;
    float stoppingDistance = 0.2f;
};
```

### 最小 Update

```cpp
void NavAgentComponent::Update(float dt)
{
    if (currentWaypoint >= currentPath.points.size())
        return;

    Vec3 target = currentPath.points[currentWaypoint];
    Vec3 toTarget = target - owner->position;

    if (Length(toTarget) < stoppingDistance)
    {
        currentWaypoint++;
        return;
    }

    Vec3 dir = Normalize(toTarget);
    owner->position += dir * speed * dt;
}
```

これは最低限です。  
実際には、回転、加減速、物理衝突、段差、停止処理が必要になります。

ただし、最初はこれで十分です。  
まずは経路が正しく出て、Agent が目的地まで歩けることを確認します。

---

## 20. デバッグ描画

ナビメッシュは可視化なしで作ると地獄です。  
必ず DebugDraw を入れます。

最低限、次を描画します。

```txt
ナビメッシュ三角形
隣接エッジ
非隣接エッジ
A* で通ったポリゴン列
Funnel 後の最終経路
開始ポリゴン
目的ポリゴン
Agent の現在ターゲット
```

### DebugDraw API 例

```cpp
NavDebugRenderer::DrawNavMesh(navMesh);
NavDebugRenderer::DrawPolyPath(polyPath);
NavDebugRenderer::DrawPath(path);
NavDebugRenderer::DrawAgent(agent);
```

特に重要なのは、隣接していないエッジの可視化です。

```txt
緑 = 接続あり
赤 = 接続なし
黄色 = 現在の経路
青 = waypoint
```

色の種類は何でもいいですが、意味は固定してください。

---

## 21. 推奨ファイル構成

```txt
Engine/
  Navigation/
    NavMesh.h
    NavMesh.cpp

    NavMeshBuilder.h
    NavMeshBuilder.cpp

    NavMeshQuery.h
    NavMeshQuery.cpp

    NavPathFinder.h
    NavPathFinder.cpp

    NavFunnel.h
    NavFunnel.cpp

    NavAgentComponent.h
    NavAgentComponent.cpp

    NavDebugRenderer.h
    NavDebugRenderer.cpp
```

全部を `NavSystem.cpp` に押し込むのは避けてください。  
最初は速く見えても、後で確実に腐ります。

---

## 22. 実装順序

### Step 1: NavMesh を読み込む

三角形を読み込んで、デバッグ描画します。

成功条件：

```txt
Blender で作った歩行可能面がエンジン内で表示される
```

---

### Step 2: 隣接情報を作る

共有エッジを検出して、ポリゴン同士を接続します。

成功条件：

```txt
隣接しているエッジが可視化できる
孤立した三角形が見つかる
```

---

### Step 3: 点が属するポリゴンを探す

マウスクリックやデバッグ座標から、どのポリゴンにいるかを表示します。

成功条件：

```txt
クリックした床の三角形がハイライトされる
```

---

### Step 4: A\* でポリゴン列を出す

開始点と目的点を指定して、ポリゴン列を探します。

成功条件：

```txt
通過ポリゴンが表示される
```

この段階では経路がジグザグでも問題ありません。

---

### Step 5: ポータル列を作る

ポリゴン列から共有エッジを取り出します。

成功条件：

```txt
通過ポータルが線として表示される
```

---

### Step 6: Funnel Algorithm を入れる

ポータル列から waypoint を作ります。

成功条件：

```txt
ポリゴン中心経路ではなく、自然な折れ線経路が表示される
```

---

### Step 7: NavAgent で追従する

Agent が経路に沿って移動します。

成功条件：

```txt
Agent が目的地まで歩く
曲がり角で極端に引っかからない
```

---

## 23. 最小版でも無視してはいけない問題

### 1. Agent 半径

キャラには半径があります。  
ナビメッシュの線ギリギリを通ると、壁にめり込みます。

最初の対応はこれで十分です。

```txt
ナビメッシュ自体を、壁から少し内側に作る
```

つまり、Blender 側で「キャラの中心点が実際に歩ける領域」を作ります。

本格対応では、次のような処理が必要になります。

```txt
メッシュ生成時にエージェント半径ぶん収縮する
Clearance 情報を持つ
複数エージェントサイズ別にナビメッシュを持つ
```

ただし、最初は手動で内側に作れば十分です。

---

### 2. 開始点・目的点がメッシュ外にある問題

Agent の位置やクリック位置がナビメッシュ上にないことは普通にあります。  
`SamplePosition` は早めに用意してください。

---

### 3. 高さ

高低差のあるマップでは、XZ だけの判定では危険です。  
候補三角形の高さとの差も見る必要があります。

---

## 24. 最初の設計で入れておくと後が楽なもの

### NavArea

地形の種類です。

```cpp
enum class NavArea : uint8_t
{
    Walk,
    Slow,
    Blocked,
    Jump,
    Door,
};
```

最初は `Walk` だけでいいですが、フィールドとして持っておくと拡張しやすいです。

```cpp
struct NavPoly
{
    uint32_t indices[3];
    NavPolyId neighbors[3];
    Vec3 center;
    float cost;
    NavArea area;
};
```

---

### Agent 設定

```cpp
struct NavAgentSettings
{
    float radius = 0.4f;
    float height = 1.8f;
    float maxSlope = 45.0f;
    float maxStepHeight = 0.3f;
};
```

最初は使わなくてもいいですが、API には入れておくと後で拡張しやすくなります。

```cpp
bool FindPath(
    const Vec3& start,
    const Vec3& goal,
    const NavAgentSettings& agent,
    Path& outPath
);
```

---

## 25. 後回しでいいもの

### 自動生成

地形メッシュから歩ける場所を抽出し、傾斜、段差、エージェント半径を考慮してメッシュ化する処理です。

これは重いテーマです。  
最初から自作するのは時間の使い方として悪いです。

まずは手動ナビメッシュで経路探索を完成させます。

---

### 動的障害物

箱が動く、ドアが閉まる、敵が道を塞ぐ。  
これはナビメッシュ本体ではなく、別レイヤーで扱うべきです。

最初は次の仕様で逃げて構いません。

```txt
静的障害物だけナビメッシュに反映
動く障害物は経路追従中の簡易回避で対応
```

---

### 群衆回避

複数 Agent が互いに避ける処理です。

これはナビメッシュではなく、次の領域です。

```txt
Steering
RVO
ORCA
Separation
Local Avoidance
```

最初に混ぜると設計が汚れます。

---

## 26. よくある失敗

### 失敗1: ポリゴン中心をそのまま移動経路にする

これはダメです。

```txt
start -> poly center -> poly center -> goal
```

A\* の結果はあくまで「通る領域」です。  
最終的な移動経路ではありません。

必ず Funnel を入れてください。

---

### 失敗2: Agent 半径を無視する

経路は正しいのに、キャラが壁に刺さります。

これはナビメッシュが「キャラの中心が通れる場所」になっていないからです。

最初は手動で内側に寄せてください。

---

### 失敗3: デバッグ描画を後回しにする

これは論外です。  
ナビメッシュは見えないと直せません。

実装順としては、次の順です。

```txt
読み込み
↓
描画
↓
隣接描画
↓
探索
```

描画なしで A\* を書き始めるのは時間の無駄です。

---

### 失敗4: AI ロジックと NavSystem を混ぜる

悪い例：

```cpp
Enemy::Update()
{
    if (playerVisible)
    {
        // path finding
        // movement
        // avoidance
        // attack logic
    }
}
```

AI は意思決定、NavSystem は経路、Agent は移動。  
分けてください。

---

## 27. 最初の完成目標

最初の完成目標はこれで十分です。

```txt
1. Blender で作ったナビメッシュを読み込める
2. エンジン内でナビメッシュを表示できる
3. 開始点と目的点を指定できる
4. A* でポリゴン列を出せる
5. Funnel で自然な経路を出せる
6. Agent がその経路を歩ける
```

これができれば、ナビメッシュシステムの基礎はかなり固まっています。

---

## 28. 拡張する順番

基礎ができた後の拡張順はこうです。

```txt
1. SamplePosition の高速化
2. FindContainingPoly の高速化
3. エリアコスト
4. OffMeshLink
5. 動的な通行禁止領域
6. Agent 半径対応
7. 複数ナビメッシュまたは複数 Agent タイプ
8. ローカル回避
9. タイル化
10. 自動生成
```

特に **自動生成は後ろ** です。  
ここを前に持ってくると、基礎の経路探索も固まっていないのに複雑な生成問題と戦うことになります。

---

## 29. 実装優先順位

```txt
優先度 S:
  NavMesh 読み込み
  DebugDraw
  隣接情報生成
  FindContainingPoly
  A*

優先度 A:
  Portal 抽出
  Funnel
  NavAgent 追従
  SamplePosition

優先度 B:
  エリアコスト
  高さ判定
  空間分割
  OffMeshLink

優先度 C:
  動的障害物
  複数 Agent サイズ
  ローカル回避
  自動生成
```

今やるべきは S と A だけです。

---

## 30. 最終まとめ

ナビメッシュ実装で一番危険なのは、次の考え方です。

> なんとなく AI 移動全体を作ろうとする。

これは範囲が広すぎます。

最初の目標はこれだけです。

> 任意の開始点と目的点を入力したら、ナビメッシュ上の滑らかな経路点列を返す。

その後に、次を足します。

```txt
Agent が歩く
障害物を避ける
複数 Agent を制御する
ドアやジャンプを扱う
自動生成する
```

最初から全部を一体化すると、完成しないか、完成しても直せないものになります。
