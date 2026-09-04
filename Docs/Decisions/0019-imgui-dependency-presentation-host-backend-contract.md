# ADR-0019: Dear ImGui Dependency, Presentation Host, and Backend Contract

- Status: Proposed
- Date: 2026-09-04
- Decision Owners: CueEngine Project
- Approval Required: User authorization for the limited third-party dependency exception

## Context

M12 の #162、#163、#165 は、Project Hub と最小 Editor を実動 ImGui UI から操作し、Keyboard、Cancel、
Error 表示、手動 Workflow を検証することを要求する。ADR-0018 は ImGui を Presentation Adapter に限定したが、
Dear ImGui の取得方法、License、Version 固定、Platform／Renderer Backend、Tool Host の所有権は決定していない。

現在の Repository には Dear ImGui Source、Package Manager Manifest、Submodule、`FetchContent` 定義がない。
一方、現在の Project Policy は第三者 Library、OSS Package、外部 Sample Codeを例外なく依存関係または Source として
導入しない。実動 ImGui UI は Dear ImGui の Code なしに成立しないため、現行 Policy の維持と #162 の Acceptance Gate は
同時に満たせない。

本 ADR は外部 Code を取り込まずに比較を行い、限定例外を認める場合の境界を決定する。限定例外の採否は User の
明示承認を必要とし、承認前は Proposed とする。

## Verified Facts

- Dear ImGui は MIT License で公開され、Copyright Notice と Permission Notice の同梱を要求する
- Dear ImGui は Core、Platform Backend、Renderer Backend を分離する
- Windows では公式 Win32 Platform Backend と DirectX 12 Renderer Backend が提供される
- Platform Backend は Input、Cursor、Timing、Windowing を担当する
- Renderer Backend はFont TextureとDraw DataのRenderer接続を担当する
- 公式 Documentation は Custom Backend より公式 Backend の利用を初期選択として推奨する
- Dear ImGui 自体は CueEngine 用の正式 CMake Target を提供しないため、CueEngine 側に限定された Adapter Target が必要となる

確認元:

- <https://github.com/ocornut/imgui/blob/master/LICENSE.txt>
- <https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md>
- <https://github.com/ocornut/imgui/wiki/Getting-Started>
- <https://github.com/ocornut/imgui/releases>

## Decision Drivers

- User が指定した外部 Code 非取込方針を無断で緩和しない
- Clean Checkout から同じ Source Revision を取得できる
- Configure 時の暗黙 Network Access を避ける
- External Source を First-party Source と混在させない
- UI Adapter を Runtime、Cue.RHI、D3D12 Native API から隔離する
- Tool Host 固有の Window、ImGui Context、Backend、GPU Resource の寿命を一意にする
- M12 の手動 UI Workflow と Headless Test を両立する
- 将来の Runtime Renderer、Viewport、Docking、Multi-Viewport を先取りしない

## Options

### Option A: 現行の第三者 Code 禁止を維持し、First-party UI Toolkit を実装する

第三者依存は増えないが、#162 と #163 の ImGui 要件を満たさず、Input、Text Editing、Layout、Clipping、Font、
Accessibility、Renderer Backend を M12 内で新規設計する必要がある。Project Hub と基本 Editor 操作を早期に完成させる目的に
対して Scope が大きすぎるため推奨しない。

### Option B: Dear ImGui だけに限定した明示例外を設け、公式 Repository を Pin した Git Submodule として取得する

External Source の正本、Commit Identity、License を分離でき、CueEngine Source へ Code をコピー、改名、部分抽出せずに利用できる。
Configure は既に取得済みの Pin 済み Sourceだけを使用し、Network AccessやBranch追従を行わない。初回取得には明示的な
Submodule 初期化が必要となる。

限定例外を User が承認する場合の推奨案とする。

### Option C: CMake FetchContent でDear ImGuiを取得する

Configure が Network と外部 Host 状態へ依存し、Source取得とBuild定義が暗黙に混在する。Offline Build、失敗診断、
Supply Chain Review が弱くなるため採用しない。

### Option D: vcpkg Manifest で Dear ImGui を取得する

Version と Baseline を記録できるが、Dear ImGui に加えて Package Manager、Registry、Port Definition を新たな Build 入力にする。
現時点の Repository は vcpkg を正式 Toolchain として採用しておらず、M12 UI のために全 Project の Package Policy を固定するため
採用しない。将来複数の承認済み第三者依存が必要になった場合は別 Research Issue で再評価する。

### Option E: Dear ImGui SourceをRepositoryへCopyまたはVendorする

User Policy の外部 Code 非取込と直接衝突し、更新時の差分と Provenance も曖昧になるため採用しない。

### Option F: Machineへ事前InstallされたBinaryを検索する

ABI、Compiler、Configuration、Version、License、Clean Checkout 再現性を保証できないため採用しない。

## Proposed Decision

User が限定例外を明示承認した場合だけ、Option Bを採用して本ADRをAcceptedへ変更する。

例外は次の範囲だけに限定する。

- Upstream は `https://github.com/ocornut/imgui.git` だけとする
- Release Tag と Commit SHA の両方を ADR と Dependency Manifestへ記録する
- 初期採用候補は Research時点の最新Stable Release `v1.92.9` とするが、導入直前にTagとCommitを再確認する
- Git Submodule は `External/DearImGui` に配置し、External Source と First-party Sourceを分離する
- Dear ImGui Core、公式 Win32 Backend、公式 DirectX 12 Backend だけを対象とする
- `examples/`、Demo Application、第三者Extension、Docking Branch、Multi-ViewportはBuildしない
- External Source は変更、Copy、Patch、Rename、部分抽出しない
- Upstream `LICENSE.txt` を配布物とRepositoryのThird-party Noticeへ含める
- Submodule未取得、Revision不一致、License不足はConfigure時に明示的に失敗させる
- Configure中にNetwork取得、Branch追従、自動Updateを行わない
- Updateは専用Research／Maintenance IssueでTag、Commit、License、API差分、3構成Buildを再検証する

限定例外を承認しない場合は、#162 と #163 の ImGui 要件を変更し、First-party UI Toolkitを別Milestoneへ分割する。
M12 内でImGuiを模倣するMock UIを実動UIとして完了扱いにしない。

## Target and Dependency Boundary

```text
Cue.ProjectHub.ImGui ------> Cue.ProjectHub
          |----------------> Cue.ImGui.Core

Cue.Editor.ImGui ----------> Cue.EditorCore
          |----------------> Cue.ImGui.Core

Cue.ToolHost.WindowsD3D12 -> Cue.Platform.Windows
          |----------------> Cue.ImGui.Backend.Win32D3D12
          |----------------> D3D12 / DXGI private composition

Cue.ImGui.Backend.Win32D3D12 -> Cue.ImGui.Core
          |--------------------> official Win32 / DX12 backend source
```

`Cue.ProjectHub`、`Cue.EditorCore`、`Cue.Scene`、`Cue.Project`、Runtime Module は Dear ImGuiへ依存しない。
`Cue.ProjectHub.ImGui` と `Cue.Editor.ImGui` は `Cue.RHI`、D3D12 Header、Native Device、Descriptor Heapを参照しない。

`Cue.ToolHost.WindowsD3D12` は M12 Tool UI 専用の Composition Root とする。Window、Message Pump、ImGui Context、
Frame開始／終了、Tool用D3D12 Device、Queue、Swap Chain、Descriptor Heap、公式Backendの初期化／終了順を所有する。
Tool Host の Native ObjectをPresentation AdapterまたはApplication Serviceの公開APIへ出さない。

M12 では Runtime Renderer、Game Swap Chain、Viewport Render Target、Cue.RHI 公開APIを Tool UI のために変更しない。
Tool用D3D12 ResourceとGame Renderer Resourceの共有は対象外とする。

## Ownership and Lifetime

起動順は次とする。

1. Foundation Diagnostics
2. Windows Window と Message Pump
3. Tool用D3D12 Device、Queue、Swap Chain、Descriptor Resource
4. Dear ImGui Context
5. Win32 Platform Backend
6. DirectX 12 Renderer Backend
7. Project HubまたはEditor Application Service
8. Presentation Adapter State

終了順は逆順とする。GPU Idle確認とBackend Shutdownが完了するまでDescriptor ResourceとDeviceを破棄しない。
ImGui Contextは一つのTool Hostが一意所有し、Global SingletonまたはRuntime Serviceへ登録しない。

一つのUI FrameはOwner Threadだけで処理する。Windows Message、ImGui Frame、Semantic Intent適用、Application Service Mutation、
ViewModel再取得、Draw Data提出を同じThreadで順序付ける。Background ThreadからImGui APIまたはProjectHubServiceを直接呼ばない。

## Presentation Contract

ImGui Adapterは表示用ViewとPresentation Stateだけを読み、User操作をSemantic Intentへ変換する。

- ViewのStable IdentityはProjectId、ObjectId、ComponentInstanceIdを使用する
- ImGui ID、Row Index、Pointer、Pathだけを長期Identityにしない
- Text Buffer、Focus、Popup、選択中Template、確認DialogはPresentation Stateが所有する
- Filesystem、Descriptor、Serializer、RecentProjectRegistryを直接操作しない
- Service操作後は成功／失敗にかかわらず公開Viewを再取得する
- Error CodeとContextをPresentation側の安定Mappingで日本語表示する
- Progressは実Operation Stateだけを表示し、同期処理を偽の非同期Progressとして表現しない

## Error and Diagnostics Contract

- External Dependency未取得はCMake Configure Errorとし、Downloadを自動開始しない
- Backend初期化失敗は部分Hostを逆順に破棄し、Process Exit CodeとFoundation Errorへ記録する
- Device RemovalはDRED診断を記録してTool Sessionを終了し、自動Device再生成はM12対象外とする
- UI AdapterはApplication ServiceのErrorを握りつぶさず、安定Categoryと操作対象を日本語Messageへ変換する
- `DurabilityUnknown`は成功表示に変換せず、公開済み可能性と再確認手順を表示する

## Validation Contract

Headless TestはDear ImGuiのPixel出力に依存せず、次を検証する。

- ViewModelから表示Row状態へのMapping
- Semantic Intentの生成と無効操作抑止
- Keyboard Activate、Focus移動、Escape Cancel
- Create／Register／Open／Pin／Remove確認
- Missing／Broken／Compatibility／DurabilityUnknownの日本語Message
- Service Mutation後に旧Viewを再利用しないこと

Backend Integration TestはWindowとTool D3D12 Resourceを生成し、自動Close可能なSmoke ModeでFrameを提出する。
Manual TestはProject作成、既存Project登録、Pin、一覧除外、Keyboard操作、Cancel、Editor Launch要求、正常Closeを確認する。

Pixel完全一致、Theme、Font Raster差分、Docking、Multi-ViewportはM12 Gateに含めない。

## Consequences

### Positive

- 実動ImGui UIと外部Code非混在を両立できる
- Upstream RevisionとLicenseをReview可能な形で固定できる
- Tool UIがRuntime RendererとRHI公開APIを拡張せずに成立する
- Project HubとEditor CoreをHeadlessに維持できる
- 後続のFiles、Play、Build UIが同じHost境界を再利用できる

### Trade-offs

- Userが承認する限定的な第三者Dependency例外が必要になる
- Clone後に明示的なSubmodule初期化が必要になる
- Tool専用D3D12 ResourceはRuntime RHIと実装責務が一部重複する
- Upstream Update、License Notice、Supply Chain Reviewの継続運用が必要になる
- Presentation Adapter、Host、Backendを分離するためTarget数が増える

### Mitigations

- 例外対象、Upstream、Source範囲、Version、更新手順を固定する
- External TargetへWarningとInclude境界を限定し、First-party Warningを抑止しない
- Tool Hostの重複をM12最小Scopeに限定し、Runtime Rendererへ逆流させない
- Dependency取得、Hash、License、3構成BuildをCI Gateへ追加する

## Rejected Shortcuts

### Mock SurfaceだけをImGui UIとして完了扱いにする

Headless Testには有用だが実動Window、Input、Backend、手動Workflowを満たさないため採用しない。

### Project Hub AdapterからD3D12 Deviceを直接所有する

UI責務とGPU Resource Lifetimeが混在し、#162の依存GateとADR-0018へ反するため採用しない。

### RuntimeHostまたはGame Swap ChainへProject Hubを埋め込む

Tool起動にRuntime WorldとGame Rendererを要求し、RuntimeからEditorへの依存を作るため採用しない。

## Required Approval

本ADRをAcceptedにする前に、次のいずれかをUserが明示する必要がある。

1. Dear ImGuiに限り、上記Pin済みGit Submoduleと公式Win32／DX12 Backendを第三者Dependency例外として承認する
2. 第三者Code禁止を維持し、#162／#163をFirst-party UI Toolkit Researchへ再計画する

承認前はSubmodule、External Source、Build設定、License Fileを追加しない。

## Follow-up

- 承認後に本ADRと`AGENTS.md`のLicense Policyを同じPRで整合させる
- #162でDependency Pin、External Target、Tool Host、Project Hub ImGui Adapterを最小実装する
- #163で同じHostへHierarchy／Inspector Adapterを追加する
- #164でProject HubとEditor Process Workflowを統合する
- #165でClean Checkout、3構成、Headless、Backend Smoke、手動UI Workflowを検証する
