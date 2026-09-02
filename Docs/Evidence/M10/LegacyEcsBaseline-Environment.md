# Legacy ECS Baseline Environment

- Date: 2026-09-02
- Legacy commit: `c3727915060df333be61f4fd72c0b51970799cd5`
- Legacy checkout: Detached HEAD、変更なし
- Host: Windows x64
- OS: Microsoft Windows 11 Home 10.0.26200、Build 26200
- CPU: AMD Ryzen 7 3700X 8-Core Processor
- Physical cores: 8
- Logical processors: 16
- Generator: Visual Studio 18 2026
- MSBuild: 18.9.1
- Compiler: MSVC 19.51.36256.0
- Windows SDK: 10.0.26100.0
- Configuration: Development
- Optimization: `/O2`
- Clock: `std::chrono::steady_clock`
- Worker threads: なし、Caller Threadのみ
- Warm-up: 3回
- Recorded iterations: 10回
- Entity counts: 1,000、10,000、100,000

結果Fileは同じProcess内の異なるEntity数を混在させず、Entity数ごとにBenchmark Executableを起動して生成した。
測定中のCPU Affinity固定、OS Service停止、電源Plan変更は行っていないため、結果はこのMachine上の初期Baselineとして扱い、
異なるMachineまたは電源条件の数値と直接比較しない。

## Result Files

- `LegacyEcsBaseline-1000.json`
- `LegacyEcsBaseline-10000.json`
- `LegacyEcsBaseline-100000.json`

絶対性能や新ECSの優位性はこの測定だけでは主張しない。新ECS実装後、同じHost、Compiler、Configuration、Workload、
Warm-up、反復回数で再測定し、Safety契約の差と併記する。
