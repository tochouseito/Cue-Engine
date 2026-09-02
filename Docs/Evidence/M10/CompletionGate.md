# M10 Schema, ECS, and Runtime World Completion Gate

## Gate Result

M10の先行Issue 7件はGitHub上でClosedであり、本Gate Issue #148がMilestone最後の1件であることを2026-09-03に確認した。
本変更のPRが`Closes #148`でMergeされた時点で、M10は8/8 Closedとなる。

| Acceptance Gate | Result | Evidence |
|---|---|---|
| M10配下の全Issue | Pass予定 | 先行7件Closed、本PRが最後の#148をCloseする |
| Debug／Development／Release Build | Pass | 全Targetを3構成でBuild成功 |
| CTestとHeadless Process Test | Pass | 3構成とも182/182失敗0、`Cue.GameCore.Process.HeadlessRuntimeWorld`成功 |
| 大量Entity測定条件とBaseline | Pass | 1,000／10,000／100,000 Entity、3 Warm-up、10 Sample、中央値／p95を記録 |
| 未測定の性能向上主張なし | Pass | 数値は初期Baselineと改善候補に限定 |
| 未実行検証と残るRisk | Pass | 本文末尾へ明記 |

Releaseの全CTestでは、Debug Layer／InfoQueue／DREDを必要とする既存4 Testが構成条件によりSkippedとなった。
M10対象の`GameCore` Label 11 TestとHeadless Process Testは全構成で実行され、成功した。

## Coverage Map

| M10 Scope | Verification |
|---|---|
| Entity再利用Stress | `Cue.GameCore.Storage`の10,000 Entity再利用検証、100,000 Entity Benchmark |
| Component寿命 | `Cue.GameCore.Storage`のMove-only Component破棄回数と逆Storage破棄順 |
| Query／Command順序 | `Cue.GameCore.Storage`とStructural／Query Process TestのFIFO、再入、例外境界 |
| Schema登録順変更 | `Cue.Schema.Registry`のStable IDとDense Index分離検証 |
| Headless World Process | `Cue.GameCore.Process.HeadlessRuntimeWorld` |
| 依存方向 | Configure時の`Cue.GameCore` Link制約とSource Include検査 |

`Cue.GameCore`は公開・直接Linkとも`Cue.Foundation`、`Cue.Math`、`Cue.Schema`だけを許可する。
GameCore SourceとBenchmarkに`Cue.Project`、`Cue.Platform`、`Cue.RHI`、`Cue.Editor` Includeがないことを確認した。

## 100,000 Entity Median Snapshot

単位はMillisecondであり、p95と全Entity数の結果は対応するJSONを正本とする。

| Workload | Legacy median | Rebuild median | Observation |
|---|---:|---:|---|
| `entity_generate` | 10.0870 | 2.94950 | 次の計測で再現性を確認する候補 |
| `entity_destroy_reuse` | 29.5668 | 436.406 | 大規模Free List／全Storage走査の調査候補 |
| `component_add` | 66.3325 | 432.816 | Sparse拡張とCapability検証の調査候補 |
| `component_get_sequential` | 1.31070 | 3.85610 | 検証CostとCache挙動の調査候補 |
| `component_remove` | 8.02720 | 5.50900 | 次の計測で再現性を確認する候補 |
| `query_two_components` | 7.58990 | 1.02620 | Storage交差方法の比較候補 |
| `deferred_component_remove` | 8.53485 | 19.4596 | Command Result生成を含むSafe Point Costの調査候補 |

この表は単一Machineの初期観測であり、性能改善またはRegressionの確定判定ではない。
両実装は同じ意味のWorkloadを実行するが、Entity検証、Capability、System dispatch、Deferred CommandのSafety契約が異なる。

## Validation Commands

- `cmake --preset windows-vs2026`
- `cmake --build --preset windows-vs2026-debug`
- `cmake --build --preset windows-vs2026-development`
- `cmake --build --preset windows-vs2026-release`
- `ctest --preset windows-vs2026-debug`
- `ctest --preset windows-vs2026-development`
- `ctest --preset windows-vs2026-release`
- `CueGameCoreBenchmark.exe --entities 1000 --warmup 3 --iterations 10 ...`
- `CueGameCoreBenchmark.exe --entities 10000 --warmup 3 --iterations 10 ...`
- `CueGameCoreBenchmark.exe --entities 100000 --warmup 3 --iterations 10 ...`
- JSON parse、Workload順序、Commit Identity、`git diff --check`、依存Include検査
- Build直前のCommit不一致拒否と測定対象Source未Commit変更拒否

## Not Run

- AddressSanitizer、ThreadSanitizer、UndefinedBehaviorSanitizer
- 数時間以上のECS Soak Test
- 複数Machine、CPU Affinity固定、電源Plan固定での再測定
- Hardware CounterまたはProfilerによるCache Miss、Allocation、Branch Costの分解
- Parallel ECS検証

## Remaining Risks

- RebuildのEntity破棄は全Component Storageを走査するため、Storage種類増加時のCostが未測定
- Packed StorageのSparse配列拡張が大規模Component追加の支配要因である可能性は未分析
- Queryは現時点で単一Owner Thread契約であり、Parallel QueryとSchedulerは対象外
- BenchmarkはOS SchedulingとBackground Loadを制御していないため、数値の継続判断には再測定が必要
- Scene、Prefab、Project永続形式とRuntime Entityの対応はM10対象外
- Serializer、Migration、Editor Undo／RedoからSchemaを利用する統合は後続Milestone対象
