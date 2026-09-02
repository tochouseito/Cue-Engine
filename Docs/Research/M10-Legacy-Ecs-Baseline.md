# M10 Legacy ECS Baseline and Comparison Contract

## Purpose

M10の新ECSを設計する前に、旧CueEngine ECSが解決していた問題、現在の性能特性、制約を測定可能なBaselineとして固定する。
旧SourceをRebuildへコピー、移植、改名、部分抽出せず、旧版と新版を別Executableとして同一Workloadで比較する。

## Legacy Dependency Audit

`origin/release`の旧ECS Targetは`Base`と`Core`をLinkする定義だが、ECS Source自身の直接依存を調査すると、比較対象のCore操作に
必要なSourceはECSManager、Component ID、`IClock`、`TimeSpan`に限定できる。隔離Benchmarkは次だけをCompileする。

- 旧ECSの`ECSManager.cpp`、`EngineComponentID.cpp`と対応Header
- 旧CueEngine First-partyの`IClock.h`、`TimeUnit.h`
- Rebuildで新規作成したBenchmark Driver
- C++ Standard LibraryとToolchain API

旧版のJolt、ImGui、DirectX関連Sample、Renderer、Editor、Scripting、Asset ModuleはCompile、Link、実行しない。Rebuildの通常
Configure、Build、CTestも旧Checkoutを探索せず、旧ECS Targetを含まない。

## Isolation Contract

- 旧ECSは`origin/release`のDetached Worktreeから読み取る
- WorktreeのGit Root、Commit SHA、Clean状態をConfigure時とBuild直前に検証して結果へ対応付ける
- Benchmark Build DirectoryはRebuildの通常Presetと分離する
- 旧Checkoutへ生成物、Patch、設定Fileを書き込まない
- Rebuild Runtime、GameCore、Schemaから旧ECS HeaderまたはBinaryを参照しない
- 比較結果は設計判断の入力であり、旧APIまたはLayoutを新API要件にしない

## Comparable Workloads

| Name | Measured operation | Operation count |
| --- | --- | ---: |
| `entity_generate` | 空WorldへEntityを生成 | N |
| `entity_destroy_reuse` | N Entityを破棄し、同数を再生成 | 2N |
| `component_add` | 生成済みEntityへPositionを追加 | N |
| `component_get_sequential` | Entity順にPositionを取得 | N |
| `component_remove` | 全EntityからPositionを削除 | N |
| `query_two_components` | Position＋Velocityを要求するSystemを1回走査 | N |
| `deferred_component_remove` | N件の遅延削除CommandをSafe Pointで反映 | N |

Setupは測定区間外で実行する。結果が未使用として最適化されないよう、各Workloadは最終状態からChecksumを生成する。

## Statistics and Metadata

- `std::chrono::steady_clock`を使用する
- 3回のWarm-up後、10回を記録する
- Sampleを昇順に並べ、中央値とp95を記録する
- operations/secondは`operationCount / median seconds`で算出する
- Entity数1,000、10,000、100,000を別Processで測定する
- Legacy Commit、Compiler、Build Configuration、CPU、OS、Thread条件を記録する
- 同じMachine上で旧版、新版の順序を交互に実行することを推奨する

絶対値だけで新設計を決定しない。新ECSは古いHandle拒否、Component寿命、Query中Mutation、決定的Command順序など、旧版にない
Safety Gateを満たすため、その追加Costを機能差と併記する。

## Decision Use

Issue #143では、Storage候補の選定時に次を区別する。

1. 旧ECSに対する実測差
2. Safety契約追加によるCost
3. Storage Layoutによる差
4. Measurement Noise内の差

測定していないWorkloadの性能向上を主張しない。結果がMachine、Compiler、構成の違いを含む場合は直接比較せず、新旧を同一条件で
再測定する。
