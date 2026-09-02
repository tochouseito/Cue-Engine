# Rebuild ECS Baseline Environment

- Date: 2026-09-03
- Rebuild benchmark commit: `a3323147d08427becb52c10e23f30bfc7232021c`
- Host: Windows x64
- OS: Microsoft Windows 11 Home 10.0.26200、Build 26200.9168
- CPU: AMD Ryzen 7 3700X 8-Core Processor
- Physical cores: 8
- Logical processors: 16
- Generator: Visual Studio 18 2026
- MSBuild: 18.9.1
- Compiler: MSVC 19.51.36256.0
- Windows SDK: 10.0.26100.0
- CMake: 4.2.3
- Configuration: Development
- Optimization: `/O2`
- Clock: `std::chrono::steady_clock`
- Worker threads: なし、Caller Threadのみ
- Warm-up: 3回
- Recorded iterations: 10回
- Entity counts: 1,000、10,000、100,000

`CueGameCoreBenchmark`はEntity数ごとにProcessを分け、各WorkloadのSetupを測定区間外で再構築した。
結果は10 Sampleの中央値、p95、処理件数／秒を記録し、各Sampleの結果が消去されないよう検証値もJSONへ保存した。

測定中のCPU Affinity固定、OS Service停止、電源Plan変更は行っていない。
旧Baselineと同一Host、Generator、Compiler、SDK、Development構成、Warm-up、反復回数を使用したが、
OS SchedulingとBackground Loadの揺らぎを含むため、絶対性能や他Machineとの比較には使用しない。

## Result Files

- `RebuildEcsBaseline-1000.json`
- `RebuildEcsBaseline-10000.json`
- `RebuildEcsBaseline-100000.json`

各JSONの`rebuildCommit`は測定ExecutableへConfigure時に埋め込んだBenchmark実装Commitを示す。
結果Fileを追加した後続Commitではなく、測定対象Codeを再現するためのIdentityとして扱う。
Benchmark TargetをBuildするたびに、HEADが埋め込み対象Commitと一致し、Benchmark、Foundation、Math、Schema、GameCoreの測定入力がCleanであることを再検証する。

## Comparison Limits

旧ECSとRebuild ECSは同じ7 Workload名、Entity数、Warm-up、反復、集計方法を使用するが、内部Safety契約は同一ではない。
RebuildはWorld Identity、世代、Component Capability、Owner Thread、Result、Structural Command Reportを検証する。
旧ECSは異なるEntity表現、System dispatch、Deferred Function Queueを使用する。

したがって数値は設計改善候補を見つける初期Baselineであり、速度比だけで実装の優劣を結論付けない。
