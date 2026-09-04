# ADR-0019: Dear ImGui Dependency, Presentation Host, and Backend Contract

- Status: Accepted
- Date: 2026-09-04
- Decision Owners: CueEngine Project
- Approval: User authorized Dear ImGui through vcpkg on 2026-09-04

## Context

M12 の #162、#163、#165 は、Project Hub と最小 Editor を実動 ImGui UI から操作し、Keyboard、Cancel、
Error 表示、手動 Workflow を検証することを要求する。ADR-0018 は ImGui を Presentation Adapter に限定したが、
Dear ImGui の取得方法、License、Version 固定、Platform／Renderer Backend、Tool Host の所有権は決定していない。

現在の Repository には Dear ImGui Source、Package Manager Manifest、Submodule、`FetchContent` 定義がない。
Userは第三者CodeをEngine所有Sourceへ混在させず、Licenseを順守して`ThirdParty`配下へ分離し、外部Libraryを
vcpkgで導入する方針を指定した。新規外部Libraryは導入前に毎回Userの明示承認を必要とし、Dear ImGuiはM12での導入が
明示承認された。

本 ADR は承認済みDear ImGuiの取得、License、Version固定、Presentation Host、Backend、所有権境界を決定する。

## Verified Facts

- Dear ImGui は MIT License で公開され、Copyright Notice と Permission Notice の同梱を要求する
- Dear ImGui は Core、Platform Backend、Renderer Backend を分離する
- Windows では公式 Win32 Platform Backend と DirectX 12 Renderer Backend が提供される
- Platform Backend は Input、Cursor、Timing、Windowing を担当する
- Renderer Backend はFont TextureとDraw DataのRenderer接続を担当する
- 公式 Documentation は Custom Backend より公式 Backend の利用を初期選択として推奨する
- Dear ImGui 自体は CueEngine 用の正式 CMake Target を提供しないため、CueEngine 側に限定された Adapter Target が必要となる
- vcpkgはManifest Modeを多くのUserに推奨し、Manifestごとに分離されたInstall Treeを使用する
- vcpkgの`builtin-baseline`はRegistry Commitを固定し、Dependency Versionの再現性を提供する
- 確認時点のvcpkg builtin portはDear ImGui `1.92.6`と`win32-binding`／`dx12-binding` Featureを提供する

確認元:

- <https://github.com/ocornut/imgui/blob/master/LICENSE.txt>
- <https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md>
- <https://github.com/ocornut/imgui/wiki/Getting-Started>
- <https://github.com/ocornut/imgui/releases>
- <https://learn.microsoft.com/vcpkg/concepts/manifest-mode>
- <https://learn.microsoft.com/vcpkg/users/examples/versioning.getting-started>
- <https://github.com/microsoft/vcpkg/tree/master/ports/imgui>

## Decision Drivers

- User承認のない外部Libraryを導入しない
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

### Option B: 公式RepositoryをPinしたGit Submoduleとして取得する

External Source の正本、Commit Identity、License を分離でき、CueEngine Source へ Code をコピー、改名、部分抽出せずに利用できる。
Configure は既に取得済みの Pin 済み Sourceだけを使用し、Network AccessやBranch追従を行わない。初回取得には明示的な
Submodule 初期化が必要となる。

Versionは固定できるが、Userが指定したvcpkg限定方針に反するため採用しない。

### Option C: CMake FetchContent でDear ImGuiを取得する

Configure が Network と外部 Host 状態へ依存し、Source取得とBuild定義が暗黙に混在する。Offline Build、失敗診断、
Supply Chain Review が弱くなるため採用しない。

### Option D: vcpkg Manifest で Dear ImGui を取得する

ManifestとRegistry BaselineでDependency Graphを宣言し、Project専用Install Treeへ分離できる。Package Manager、Registry、
Port DefinitionもBuild Inputになるため、それらをVersion PinとReview対象へ含める。Userがvcpkgでの外部Library導入を指定したため
採用する。

### Option E: Dear ImGui SourceをRepositoryへCopyまたはVendorする

Engine Sourceとの分離は可能だが、vcpkgを唯一の導入経路とするPolicyに反し、更新時の差分とProvenanceも曖昧になるため採用しない。

### Option F: Machineへ事前InstallされたBinaryを検索する

ABI、Compiler、Configuration、Version、License、Clean Checkout 再現性を保証できないため採用しない。

## Decision

Option Dを採用し、Dear ImGuiをvcpkg Manifest Modeで導入する。

- Dependency Control PlaneはRepository Rootの`ThirdParty`配下に置く
- `ThirdParty/vcpkg.json`へDear ImGui Core、`win32-binding`、`dx12-binding`だけを宣言する
- `ThirdParty/vcpkg-configuration.json`で公式vcpkg Registryと40文字のBaseline Commitを固定する
- `ThirdParty/vcpkg-tool.json`で公式vcpkg Repository、Tool Commit
  `f8be6942c0c5abd48bb325726d57af9ac39e251d`、Tool Release `2026-03-04`、Windows x64 Tool Version
  `2026-03-04-4b3e4c276b5b87a649e66341e11553e8c577459c`、実行Binary SHA-256
  `13a1c66b9c7578427b3eda7eba2332b73d4fb86706e053ad6426dad2f354cbc3`、Tool Source SHA-512
  `5eeffe70ab71a4d1ea1a836b5c16b60fbd318bfe1d4473bd2b9e03e089e81508b00d3b9368b2a1a8423010d9bf479500a00f03524f4e88aa3d444c2ef3b30ca1`
  を固定する
- 初期導入は確認済みbuiltin portのDear ImGui `1.92.6`を使用し、導入時のBaselineでVersionを固定する
- `ThirdParty/vcpkg_installed`をProject専用Install Rootとし、生成物としてGit管理対象外にする
- `ThirdParty/.tools/vcpkg`は明示Dependency Restoreだけが作成できるPin済みTool Checkoutとし、Git管理対象外にする
- `ThirdParty/THIRD_PARTY_NOTICES.md`と`ThirdParty/Licenses/DearImGui-LICENSE.txt`をGit管理し、配布物にも含める
- `examples/`、Demo Application、第三者Extension、Docking Feature、Multi-Viewportは対象にしない
- 第三者Sourceは変更、Copy、Patch、Rename、部分抽出しない
- `Engine`配下には第三者Source、Header、Binary、License Copyを配置しない
- Dependency Restoreは専用Script／CI Stepとして明示実行し、通常のCMake Configure中の暗黙Network取得は無効にする
- Dependency Restoreは`ThirdParty/.tools/vcpkg`の管理Checkoutだけを実行元とし、外部`VCPKG_ROOT`を使用しない
- Restore前に管理Checkoutの追跡対象WorktreeがCleanであること、HEAD Commit、
  `scripts/vcpkg-tool-metadata.txt`のRelease／Source Hash、`vcpkg.exe version`、実行Binary SHA-256を
  `vcpkg-tool.json`と照合する
- 初回または実行Binary不一致時はPin済みClean Checkoutの`bootstrap-vcpkg.bat -disableMetrics`から再生成し、
  再照合に失敗した場合はInstallを開始せず失敗する
- Machine固有の絶対PathをRepositoryへ記録しない
- Updateは専用Research／Maintenance IssueでUser承認を得て、Baseline、Version、License、API差分、3構成Buildを再検証する
- Dear ImGui以外の外部LibraryをManifestへ追加する場合は、その変更前にUserの明示承認を得る

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

Cue.ImGui.Core ------------> vcpkg imgui::imgui
```

`Cue.ProjectHub`、`Cue.EditorCore`、`Cue.Scene`、`Cue.Project`、Runtime Module は Dear ImGuiへ依存しない。
`Cue.ProjectHub.ImGui` と `Cue.Editor.ImGui` は `Cue.RHI`、D3D12 Header、Native Device、Descriptor Heapを参照しない。

`Cue.ToolHost.WindowsD3D12` は M12 Tool UI 専用の Composition Root とする。Window、Message Pump、ImGui Context、
Frame開始／終了、Tool用D3D12 Device、Queue、Swap Chain、Descriptor Heap、公式Backendの初期化／終了順を所有する。
Tool Host の Native ObjectをPresentation AdapterまたはApplication Serviceの公開APIへ出さない。

M12 では Runtime Renderer、Game Swap Chain、Viewport Render Target、Cue.RHI 公開APIを Tool UI のために変更しない。
Tool用D3D12 ResourceとGame Renderer Resourceの共有は対象外とする。

`Cue.ImGui.Core`はCMake上で`imgui::imgui`を`PUBLIC`または`INTERFACE`依存として公開し、Presentation Adapterへ
Core HeaderとLink Symbolを供給する。`Cue.ImGui.Backend.Win32D3D12`は同じTargetの公式Win32／DX12 Backend APIを
First-party Host Adapterから呼ぶ。Presentation AdapterはBackend Targetへ依存しない。

## Win32 Message Delivery Contract

公式Win32 BackendへInputを渡すため、#162で`Cue.Platform.Windows`にWindows固有のOpt-in Message Sink境界を追加する。
Platform非依存の`Cue.Platform` API、`WindowEvent`、Runtime ModuleへWin32型を追加しない。

- Sinkのattach／detachはWindow Owner Threadだけで行い、一つのWindowに一つだけ非所有Sinkを関連付ける
- SinkはNative Window、Message ID、`wParam`、`lParam`を`const void*`、固定幅Integer、`std::uintptr_t`、
  `std::intptr_t`のWindows固有Value Viewとして受け、Headerから`windows.h`を公開しない
- SinkはHandled FlagとNative Result値を返し、Host Adapterが`ImGui_ImplWin32_WndProcHandler`の結果へ変換する
- `WM_CLOSE`、`WM_DESTROY`、`WM_SIZE`などPlatformが所有するLifecycle Messageは既存状態更新を優先し、
  Sinkがその処理を抑止できない
- Keyboard、Mouse、Text、FocusなどPlatformが所有しないMessageはSinkを呼び、Handledなら`DefWindowProcW`へ渡さない
- Sink CallbackはWindow Procedure内で同期実行し、例外を送出せず、Message引数とNative Windowを保存しない
- SinkはImGui ContextとWin32 Backendより後にattachし、Backend ShutdownとContext破棄より前にdetachする
- Window破棄または初期化RollbackはSink関連付けを解除し、Sinkより後にWindowを破棄する
- `NativeWindowView`のSubclass禁止は維持し、Tool Hostによる`SetWindowLongPtrW`置換を許可しない

attach／detachは診断可能な`Result<void>`を返し、回復可能な失敗は`Cue.Platform.Windows` Error Categoryで表す。
Owner Thread外からの呼出しとSink Callback実行中の再入attach／detachは既存Window APIと同じProgramming Contract違反とし、
Debug／DevelopmentではAssertして終了し、Releaseでも状態を変更しない。Windows Windowではない対象は
`InvalidWindowKind`、Close要求済みまたは破棄済みWindowへのattachは`MessageSinkUnavailable`を返す。

関連付けの状態遷移は次の契約とする。

| 操作 | 現在状態 | 結果 |
| --- | --- | --- |
| attach(A) | 未関連付け、Windowが利用可能 | Aを関連付けて成功する |
| attach(A) | Aを関連付け済み | 冪等に成功し、状態を変更しない |
| attach(B) | Aを関連付け済み | `MessageSinkAlreadyAttached`を返し、Aを維持する |
| detach(A) | Aを関連付け済み | 関連付けを解除して成功する |
| detach(A) | 未関連付け | 冪等に成功し、状態を変更しない |
| detach(B) | Aを関連付け済み | `MessageSinkMismatch`を返し、Aを維持する |

失敗したattach／detachはCallbackを呼ばず、既存関連付けとWindow Lifecycle状態を変更しない。Window破棄は、以後Callbackが
発生しない状態へ遷移してから関連付けを自動解除する。このためWindow破棄後の同じSinkのdetachは未関連付けとして冪等に成功するが、
Tool Hostの正常終了経路はBackend ShutdownとContext破棄より前に明示detachする。安定Error Code値は#162の実装時に
既存Platform Error規約へ追加し、上記Categoryと状態不変条件をTestで固定する。

この境界はTool UIのNative Input配送に限定し、一般Runtime Input System、IME抽象、Drag and DropはM12対象外とする。

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

- External Dependency未復元は専用Dependency Checkで失敗させ、通常のCMake ConfigureからDownloadを自動開始しない
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
- Registry Baseline、Port Version、LicenseをReview可能な形で固定できる
- vcpkg Tool自体のRevisionも開発機とCIで照合できる
- Tool UIがRuntime RendererとRHI公開APIを拡張せずに成立する
- Project HubとEditor CoreをHeadlessに維持できる
- 後続のFiles、Play、Build UIが同じHost境界を再利用できる

### Trade-offs

- vcpkg本体、Registry、Port DefinitionがBuild Inputに増える
- Clean Checkout後に明示的なDependency Restoreが必要になる
- Tool専用D3D12 ResourceはRuntime RHIと実装責務が一部重複する
- Upstream Update、License Notice、Supply Chain Reviewの継続運用が必要になる
- Presentation Adapter、Host、Backendを分離するためTarget数が増える

### Mitigations

- 承認対象、vcpkg Tool Commit、Registry、Feature、Version、更新手順を固定する
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

## Approval Record

2026-09-04にUserは、第三者Codeを`Engine`から分離して`ThirdParty`配下でLicenseに従い管理すること、今後の外部Libraryは
導入前に毎回確認すること、導入手段をvcpkgへ限定すること、Dear ImGuiをM12へ導入することを明示承認した。

## Follow-up

- #162でDependency Pin、External Target、Tool Host、Project Hub ImGui Adapterを最小実装する
- #163で同じHostへHierarchy／Inspector Adapterを追加する
- #164でProject HubとEditor Process Workflowを統合する
- #165でClean Checkout、3構成、Headless、Backend Smoke、手動UI Workflowを検証する
