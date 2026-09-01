# M09 Project Data and Workspace

## Purpose

Editor UI、Scene、Scriptingより先に、Game Projectを安全に作成、検証、再Openし、User Workspaceで管理できる
Project管理Coreを確立する。Project共有Dataと端末固有Dataを分離し、Projectの移動や欠損、重複、互換性不一致を
破壊的な操作へ変換せず診断できることを完了条件とする。

## Completed Issues

- #134 `Project-134-[M09][Research] Project Descriptor・Workspace・Identity境界を決定する`
- #135 `IO-135-[M09][Research] Project Path・Filesystem・Atomic Storage契約を決定する`
- #136 `IO-136-[M09][Implementation] Project向けPath・Filesystem・Atomic Storageを実装する`
- #137 `Project-137-[M09][Implementation] Version付きProject Descriptorの解析・検証を実装する`
- #138 `Project-138-[M09][Implementation] Project TemplateとAtomic Generatorを実装する`
- #139 `Project-139-[M09][Implementation] Recent Project RegistryとWorkspace Storageを実装する`
- #140 `Project-140-[M09][Implementation] Engine・Hardware互換性判定を実装する`
- #141 `Integration-141-[M09][Gate] Project Data and Workspace Completion Gateを検証する`

## Completion Design

- `ProjectId`をProjectの永続Identityとし、Directory Pathは移動可能なLocatorとして扱う。
- `CueProject.json`はProject共有Data、`CueWorkspace.json`はUser固有のRecent Registryとして分離する。
- Project Descriptor、Workspace、CompatibilityのSchema Versionを明示し、未知または未対応Versionを拒否する。
- `Cue.IO`が正規化相対Path、Root境界、Reparse Point拒否、Atomic File Replace、Staging公開を所有する。
- Blank ProjectはStaging内でDescriptorを書込み、再読込み検証後に最終Directoryへ一度だけ公開する。
- Recent Registryは登録、再Open、Pin、Missing、移動後の明示的再関連付け、一覧除外を扱う。
- 一覧除外はRegistry Entryだけを削除し、Project Directoryを削除しない。
- Compatibility判定はProject Schema、Engine Version範囲、Required CapabilityをSynthetic Snapshotと比較し、
  Open可否とRuntime Feature Enablementを分離する。
- `Cue.Project`は`Cue.Foundation`と`Cue.IO`だけに依存し、Runtime、Editor、Rendererへ依存しない。

## Source and License Policy

M09はCueEngine用に新規設計・実装したFirst-party Codeだけで構成する。新しいThird-party／OSS Library、Sample Code、
外部Source、旧CueEngine Sourceは導入、コピー、移植していない。使用する外部InterfaceはC++ Standard Library、
Windows SDK、CMake、Compilerが提供するToolchain APIに限定する。

## Validation Environment

- Date: 2026-09-02
- Host: Windows x64
- Build system: CMake Visual Studio generator
- MSBuild: 18.9.1
- Compiler: MSVC
- Windows SDK: 10.0.26100.0

## Validation Results

| Configuration | Build | CTest | Result |
|---|---:|---:|---|
| Debug | Success | 169 / 169 | Success |
| Development | Success | 169 / 169 | Success |
| Release | Success | 169 / 169 | Success、診断専用4件は設計どおりSkip |

`Cue.Project.M09.Process`は各構成で成功した。このProcess Testは実Filesystem上で次を一つのFlowとして検証する。

1. Blank Projectの作成、Descriptor再読込み、内容一致
2. Project Schema、Engine Version、Synthetic CapabilityによるOpen互換性判定
3. Recent登録、Pin、WorkspaceへのAtomic保存、再読込み
4. Project Directory移動、Missing化、Duplicate ProjectId拒否、明示的な再関連付け、再Open
5. 既存Projectへの上書き拒否と、Project名に指定されたNested Pathによる意図しない書込みの拒否
6. Recent一覧から除外した後もProject Directoryと`CueProject.json`が残ること
7. WorkspaceとProject共有Dataが相互のRootへ書き込まれず、想定外のTop-level Entryがないこと

`Cue.Project.Generator`はCreate Directory、Descriptor Write、Descriptor Verify、Publishの各失敗を注入し、
Publish前の失敗で完成DirectoryとStagingが残らないことを検証する。Rollback失敗はPrimary Errorを保持したまま
Secondary診断へ記録し、Publish後のDurability不明は完成Directoryを破壊的にRollbackしない。

`Cue.IO.Storage`は`..`、Absolute Path、UNC、Drive指定、禁止文字、Windows予約名、Reparse Pointを拒否し、
Project Root外への読み書きを許さない。公開APIの静的確認では、`Cue.Project`にProject Directory削除操作はなく、
`remove_project`はRecent Registry Entryだけを対象とすることを確認した。

ReleaseでSkipした診断専用Testは次の4件である。

- `Cue.RHI.D3D12.FrameCommand.InfoQueue300`
- `Cue.RHI.D3D12.RtvHeap.InfoQueue`
- `Cue.RHI.D3D12.SwapChain.InfoQueue`
- `Cue.RHI.D3D12.SwapChain.DeviceRemovalDredFailure`

## Validation Commands

```text
cmake --build --preset windows-vs2026-debug --parallel
ctest --preset windows-vs2026-debug --output-on-failure
cmake --build --preset windows-vs2026-development --parallel
ctest --preset windows-vs2026-development --output-on-failure
cmake --build --preset windows-vs2026-release --parallel
ctest --preset windows-vs2026-release --output-on-failure
git diff --check
```

## Acceptance Gates

- [x] M09の先行Issue #134から#140が完了している
- [x] Debug、Development、ReleaseのBuildが成功する
- [x] 全CTestに失敗がなく、Project Process Testが成功する
- [x] Project作成のPublish前失敗後に半完成Directoryが残らない
- [x] Path境界とProcess TestでProject外への書込みがないことを確認する
- [x] Project Folder削除APIがなく、Recent一覧除外後もProjectが残る
- [x] 実行記録と未検証範囲を本文書へ残す
- [x] 新しいThird-party／OSS CodeまたはDependencyを導入しない

## Unrun Validation

- 非Windows Host、Arm64、異なるWindows Version、非NTFS File Systemでは未実行である。
- Network Drive、Cloud同期Folder、Long Path上のProjectでは未実行である。
- Process強制終了、OS Crash、電源断をAtomic ReplaceまたはDirectory Publishの瞬間に発生させる実機試験は未実行である。
- 複数Processから同じ`CueWorkspace.json`を同時更新する競合試験は未実行であり、M09は単一Writerを前提とする。
- Editor上のProject Hub、Scene編集、ScriptingはM09対象外のため未実装・未検証である。

## Remaining Risks

- Atomic保存はFile SystemとStorage DeviceのDurability特性に依存し、電源断後の物理永続性を完全には保証できない。
- Workspaceは単一Writer契約であり、将来の複数Editor Process対応にはLockまたはConflict解決方針が必要である。
- Reparse Pointを安全側で拒否するため、Cloud PlaceholderやJunctionを使用するProject配置はM09ではOpenできない。
- Compatibilityは判定と診断だけを提供し、自動MigrationやRuntime Feature実装は行わない。

## Next Work

M09を閉じた後は、Project Hub UIを先行して作り込まず、Project Dataを利用するScene／ECSのAuthoringとRuntime境界を
次のMilestoneでIssue単位に実装する。
