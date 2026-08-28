# M08 Core Math and Hardware Capability

## Purpose

DirectXMathへ依存しない`Cue.Math`と、CPU／OS／GPUの対応機能を取得するCapability基盤を、
M09以降から利用できる低層契約として確立する。

## Completed Issues

- #126 `Math-126-[M08][Research] Cue.Mathの数値・座標・行列規約を決定する`
- #127 `Math-127-[M08][Implementation] Scalar・Angle・Vector型と基本演算を実装する`
- #128 `Math-128-[M08][Implementation] Matrix・Quaternion・Transformを実装する`
- #129 `Math-129-[M08][Gate] Cue.Mathの正確性とDirectXMath非依存を検証する`
- #130 `Platform-130-[M08][Research] System・Graphics Capabilityの公開契約を決定する`
- #131 `Platform-131-[M08][Implementation] System Capability Snapshotを実装する`
- #132 `RHI-132-[M08][Implementation] Graphics Capability Snapshotを実装する`
- #133 `Integration-133-[M08][Gate] Core Math and Hardware Capability Completion Gateを検証する`

## Completion Design

- `Cue.Math`はScalar、Angle、Vector、Matrix、Quaternion、Transformの正本を所有し、DirectXMathへ依存しない。
- System CapabilityはPlatform境界、Graphics CapabilityはRHI境界でImmutable Snapshotとして取得する。
- Query失敗をUnsupportedへ変換せず、NotQueried、Failed、Unsupported、Supportedを区別する。
- RuntimeHostをComposition Rootとして、Hardware Support、Engine Implementation、Runtime Enablementを
  独立Fieldで診断する。
- `Baseline3D`はBackend生成済みのため`Supported／Implemented／Enabled`とする。
- Ray Tracing、Mesh Shader、VRS、Sampler Feedback、Wave Operations、Enhanced BarriersはHardware Query結果を
  保持するが、M08ではRenderer実装対象外のため`NotImplemented／NotApplicable`とする。
- 実機検証で検出したD3D12 Ray Tracing Tier 1.2を、Native enumを公開せず`RayTracingTier::Tier1_2`として保持する。

## Validation Environment

- Host: Windows x64
- Build system: CMake Visual Studio generator
- Compiler: MSVC
- Windows SDK: 10.0.26100.0
- Hardware Adapter: NVIDIA GeForce RTX 3060
- Software Adapter: Microsoft Basic Render Driver（WARP）

## Validation Results

| Configuration | Build | CTest | Result |
|---|---:|---:|---|
| Debug | Success | 160 / 160 | Success |
| Development | Success | 160 / 160 | Success |
| Release | Success | 160 / 160 | Success、診断専用4件は設計どおりSkip |

全構成でMath、System／Graphics Capability、公開Header、依存方向、Graphics／Presentation／Render／Resize Smokeを
含む全CTestに失敗がなかった。ReleaseでSkipしたTestは次の4件である。

- `Cue.RHI.D3D12.FrameCommand.InfoQueue300`
- `Cue.RHI.D3D12.RtvHeap.InfoQueue`
- `Cue.RHI.D3D12.SwapChain.InfoQueue`
- `Cue.RHI.D3D12.SwapChain.DeviceRemovalDredFailure`

## Observed Capability Differences

System SnapshotはProcess／Native ArchitectureともにX64、Logical Processor 16、Physical Memory
17,058,983,936 bytes、Page Size 4,096 bytes、Cache Line Size 64 bytesだった。SSE2、SSE3、SSSE3、
SSE4.1、SSE4.2、AVX、AVX2、FMA、OS Extended StateはすべてSupportedとして取得した。

Hardware AdapterはNVIDIA GeForce RTX 3060、Dedicated Video Memory 12,703,498,240 bytes、非UMAだった。
Ray Tracing Tier 1.2、Mesh Shader、VRS、Sampler Feedback、Wave Operations、Enhanced BarriersをSupportedとして
取得した。WARPはMicrosoft Basic Render Driver、Dedicated Video Memory 0 bytes、UMAであり、同じOptional項目を
現在のWindows Runtime上ではSupportedとして取得した。

これらは2026-08-29時点の検証MachineとDriver／Runtimeに依存する観測値であり、Engineの固定要件ではない。
RuntimeHost診断は値の違いにかかわらず、SupportとEngine実装、有効化状態を別々に表示する。

## Acceptance Gates

- [x] M08の先行Issue #126から#132が完了している
- [x] Debug、Development、ReleaseのBuildが成功する
- [x] 全CTestで失敗がない
- [x] RuntimeHostの既存SmokeがHardware／WARPで成功する
- [x] Capability診断がSupport、Implementation、Enablementを区別する
- [x] Math公開Headerと依存方向を検査し、DirectXMath非依存を維持する
- [x] Hardware依存差分と未実行検証を記録する
- [x] `git diff --check`が成功する

## Unrun Validation

- 非Windows Host、Arm64、他CPU Architectureでは未実行である。
- RTX 3060以外のHardware Adapter、異なるDriver／Windows Runtimeでは未実行である。
- SIMD最適化、Renderer Optional Feature、GPU Resource、Shader、PipelineはM08対象外のため未実装・未検証である。
- Mathの性能BenchmarkはM08対象外であり、性能改善値は主張しない。

## Remaining Risks

- Hardware CapabilityはDriver／Runtime更新で変化するため、保存済み値を永続的なMachine同一性として使用できない。
- Optional Graphics CapabilityはHardware対応を取得できるが、Engine実装や有効化を意味しない。
- ReleaseのInfoQueue／DRED診断専用4件はCompile済みだが実行されないため、Debug／Developmentと同じ診断Coverageを持たない。
- Capability診断は現在Console Logであり、将来のEditor向け構造化表示は別Issueで設計する必要がある。

## Next Work

M08を閉じた後は、Renderer機能を先行追加せず、次のGame Project／Scene／ECS基盤MilestoneをIssue単位で開始する。
