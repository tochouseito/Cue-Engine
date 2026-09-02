# Legacy ECS Benchmark

## Purpose

旧CueEngineのECSをRebuildへコピー、移植、Linkせず、隔離Checkoutから直接CompileしてBaselineを取得する補助Toolである。
このDirectoryはRebuildのRoot `CMakeLists.txt`から参照せず、Engine、Runtime、Testの正式Build Graphへ含めない。

## Source Boundary

このTargetがCompileする旧版Sourceは次に限定する。

- `Engine/Source/Runtime/ECS/ECSManager.cpp`
- `Engine/Source/Runtime/ECS/EngineComponentID.cpp`
- 上記がIncludeするECS Header
- `Engine/Source/Runtime/Core/Time/IClock.h`
- `Engine/Source/Runtime/Math/TimeUnit.h`

Target自身はCueEngine First-party CodeとC++ Standard Library、Compiler、CMakeだけを使用する。旧版のRenderer、Physics、Editor、
Asset、Scripting、Third-party TargetはBuildまたはLinkしない。

## Isolated Checkout

`CUE_LEGACY_ROOT`には`origin/release`をCheckoutした、Rebuildとは別のWorktreeを指定する。実行前にCheckoutのCommitを固定し、
作業TreeがCleanであることを確認する。Configure時とBuild直前の両方で、Git Repository Root、Clean状態、Commit SHA一致を
検証する。Toolは旧CheckoutへFileを書き込まない。

```text
git worktree add --detach C:/Work/CueEngine-Legacy-EcsBaseline origin/release
cmake -S Tools/LegacyEcsBenchmark -B out/legacy-ecs-benchmark -G "Visual Studio 18 2026" -A x64 -DCUE_LEGACY_ROOT=C:/Work/CueEngine-Legacy-EcsBaseline
cmake --build out/legacy-ecs-benchmark --config Development --parallel
```

## Measurement Contract

- Clock: `std::chrono::steady_clock`
- Configuration: Development、MSVC `/O2`とDebug情報
- Warm-up: 3回
- Recorded iterations: 10回
- Aggregation: Median、p95、Medianから算出するoperations/second
- Entity counts: 1,000、10,000、100,000
- Thread: Caller Threadだけを使用
- Process: Entity countごとに別Processで実行

```text
out/legacy-ecs-benchmark/bin/Development/CueLegacyEcsBenchmark.exe --entities 10000 --warmup 3 --iterations 10 --output Docs/Evidence/M10/LegacyEcsBaseline-10000.json
```

CTest全体の経過時間や単一SampleをECS性能値として扱わない。比較時は同じMachine、電源設定、Compiler、Configuration、Entity数、
Warm-up、反復回数を使用し、Background負荷とCPU情報を別途記録する。
