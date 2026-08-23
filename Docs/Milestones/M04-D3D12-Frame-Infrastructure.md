# M04 D3D12 Frame Infrastructure Completion Evidence

## Purpose

この文書は、`M04 D3D12 Frame Infrastructure`でDirect Graphics Queue、Fence、2個のFrame Context、
RTV Descriptor Heap、2-buffer Flip Model Swap Chain、Back Buffer RTV、Resize Lifecycleを確立し、
M05のRender Target Clearへ進むためのAcceptance Gateを満たしたことを記録します。

検証日は2026-08-23です。Resource Barrier、Clear、Present Frame Loop、Depth Buffer、FrameGraphは
このMilestoneの対象外です。

## Scope Delivered

| Issue | Pull Request | Result |
| --- | --- | --- |
| [#46 Frames in FlightとGPU同期Ownership](https://github.com/tochouseito/CueEngine/issues/46) | [#77](https://github.com/tochouseito/CueEngine/pull/77) | 2 Frame、Queue-global Fence、Presentation Context、Resize／Shutdown契約をADR化 |
| [#47 Direct Graphics QueueとFence同期](https://github.com/tochouseito/CueEngine/issues/47) | [#78](https://github.com/tochouseito/CueEngine/pull/78) | Queue、Fence、有限Wait、Event復旧、terminal shutdownを実装 |
| [#48 Frame ContextとCommand Lifecycle](https://github.com/tochouseito/CueEngine/issues/48) | [#79](https://github.com/tochouseito/CueEngine/pull/79) | 2 Allocator、共有Command List、Frame再利用Fenceと状態機械を実装 |
| [#49 RTV Descriptor Heap Ownership](https://github.com/tochouseito/CueEngine/issues/49) | [#80](https://github.com/tochouseito/CueEngine/pull/80) | 2 slot RTV Heap、世代付きSlot、所有権診断を実装 |
| [#50 DXGI Swap ChainとBack Buffer](https://github.com/tochouseito/CueEngine/issues/50) | [#81](https://github.com/tochouseito/CueEngine/pull/81) | Windows Presentation境界、2-buffer Flip Discard、VSync／Tearing、Back Buffer取得を実装 |
| [#51 Back Buffer RTVとResize Lifecycle](https://github.com/tochouseito/CueEngine/issues/51) | [#82](https://github.com/tochouseito/CueEngine/pull/82) | Frame Context単位のBack Buffer／RTV対応、Resize／Minimize／Restore、規定順Shutdownを実装 |

正式な設計判断は[ADR-0008](../Decisions/0008-frame-flight-gpu-synchronization-ownership.md)を正本とし、
RHI／Windows境界とError契約は[ADR-0007](../Decisions/0007-minimum-rhi-d3d12-boundary.md)および
[ADR-0005](../Decisions/0005-error-assert-log-policy.md)に従います。

## Ownership and Lifecycle Evidence

- D3D12 BackendがDirect Graphics Queue、Queue-global Fence、Eventを所有する
- Presentation ContextがSwap Chain、RTV Heap、共有Graphics Command List、2個のFrame Contextを所有する
- 各Frame Contextが同じBack Buffer IndexのAllocator、reuse Fence、Back Buffer、RTV Slot、Resource Stateを保持する
- Resize前はPresentation Contextの最終Submit Fenceだけを有限時間待ちし、Queue全体の無条件Idle待ちは行わない
- GPU Idle確認後に空Command Listを`IdleClosed`へ戻し、RTV metadataと全Back Buffer参照を解放してから
  `ResizeBuffers`を呼ぶ
- Resize成功後は2個のBack Bufferと対応RTVを再取得し、Resource Stateを`Present`へ戻してFrame受付を再開する
- 0 SizeはResourceを保持したまま延期し、同一非0 SizeはNo-op、既存SizeへのRestoreはFrame受付だけを再開する
- 通常ShutdownはCommand List、Back Buffer／RTV metadata、Allocator、RTV Heap、Swap Chain、Backend登録の順で解放する
- GPU完了未証明のTimeoutは`Unavailable`としてResourceと登録を保持し、Device RemovalはDRED後にWaitなしで解放する
- GPU Idle確認後のResize段階失敗は旧ResourceへRollbackせず、Primary Errorを維持して安全にContextを`Shutdown`する

## Resize and Failure Validation

`Cue.RHI.D3D12.FrameCommand.ResizePreparationFailureMatrix`とSwap Chain Process Testで次を検証しました。

- 通常Resize、同一Size、0 Size、既存SizeへのRestore
- 50回連続する異なるSizeへのResize
- 各Resize後のBuffer Count 2、Current Back Buffer Index範囲、RTV Count 2、Format一致、Frame受付再開
- Fence Wait Timeout後の完了証明、完了／Removal未証明の`Unavailable`、Device Removal
- Allocator Reset、Command List Reset、Command List Closeの各失敗
- `ResizeBuffers`失敗、Resize後の`GetBuffer`再取得失敗、RTV再構築失敗
- GPU Idle確認後の失敗でPresentation Contextを解放し、Backend登録解除後にBackendを正常Shutdownできること
- Terminal Signal ErrorでFrame受付を停止したContextは、0 Size／同一Size／異なるSizeのResizeで再開しないこと
- GPU完了未証明時にCommand List、2 Allocator、2 Back Buffer、2 RTV、Swap Chain、RTV Heap、Backend登録、
  Queue、Fence、Eventを個別に保持すること
- Device Removal時のDRED実行時点で、Presentation Resource、Queue、Fence、Eventが解放前であること
- Debug／DevelopmentのInfoQueueとLive Object ReportにErrorがないこと

## Local Validation

Repositoryの作業Checkoutで次を実行しました。

```powershell
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug --output-on-failure
cmake --build --preset windows-vs2026-development
ctest --preset windows-vs2026-development --output-on-failure
cmake --build --preset windows-vs2026-release
ctest --preset windows-vs2026-release --output-on-failure
git diff --check
```

結果:

- Debug Build成功、CTest `111/111`成功
- Development Build成功、CTest `111/111`成功
- Release Build成功、CTestは全111件中107件成功、診断禁止方針により4件Skip、失敗0件
- Hardware／WARPのD3D12 Device SmokeとPresentation Smoke成功
- Debug／DevelopmentのFrame Command、RTV Heap、Swap Chain InfoQueue Test成功
- `git diff --check`成功

`scripts/codex_build.ps1`はRepositoryに存在しないため実行していません。正式なCMake Build／CTest Presetで
同じ3構成を検証しました。

## Acceptance Gates

- [x] Direct Graphics Queue、Fence、有限Wait、Event復旧契約が実装されている
- [x] 2個のFrame ContextとBack Buffer Indexの対応が一意である
- [x] Allocatorを対応reuse Fence完了後だけResetする
- [x] RTV Heapと2個のBack Buffer RTVが明示所有される
- [x] Windows専用Adapter以外へNative Window／D3D12型を公開しない
- [x] Resize／Minimize／Restoreと50回連続Resizeが成功する
- [x] Resize各段階の失敗後にResource保持または安全な解放を選択できる
- [x] 通常、Device Removal、Unavailableで規定のShutdown契約を維持する
- [x] Debug、Development、Releaseの全CTest Presetで失敗がない
- [x] Hardware／WARP Presentation Smokeが成功する
- [x] M04 Acceptance GateのEvidenceをこの文書へ集約した

## Known Risks and Deferred Work

- M04はBack Buffer Stateを`Present`として追跡しますが、Resource Barrierは記録しません。
  `Present -> RenderTarget -> Present`はM05で実装します。
- Present Frame Loopを実装していないため、Resize前のFence待機はFrame Command StateのSubmit／Wait Matrixと
  RuntimeHost Presentation Smokeで検証しています。実描画Workを含む連続Present／ResizeはM05の範囲です。
- Releaseでは設計どおりD3D12 Debug LayerとDREDを有効化しないため、4件の診断依存TestをSkipします。
- Windows x64、Visual Studio 2026、MSVC、DirectX 12以外のPlatform／Compiler／Graphics APIは未検証です。

## Next Work

次のMilestoneでは、M04の所有権と同期契約を変更せず、Back Bufferの
`Present -> RenderTarget -> Present` Barrier、Clear、Present Frame Loopを最小範囲で追加します。
