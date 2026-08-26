# ADR-0004: Runtime Foundation Module Boundaries

- Status: Accepted
- Date: 2026-08-21
- Decision Owners: CueEngine Project
- Superseded in part by: ADR-0009（Windows UTF 変換に必要な Cue.Foundation.Windows Target と依存規則）

## Context

M01では、WindowやGraphicsを実装する前に、PlatformとRenderingから独立したRuntime Foundationを確立する必要があります。Module境界が未定のまま実装すると、Windows型やD3D12型が上位APIへ漏れ、Platform実装、RHI Backend、Runtime起動処理の所有権と依存方向が固定されるRiskがあります。

このADRはFoundationの責務、CMake Target、Source配置、依存方向、公開Header、所有権、Lifetime、Thread Affinityの基準を決定します。Error、Assert、Logの詳細方針は後続ADRで決定します。

## Legacy Reference

### Legacy Problem

旧CueEngineは、基本型と共通処理、Platform抽象化、Rendering抽象化、Engine起動処理を分離する必要がありました。

### Legacy Approach

旧実装にはBase、Core、PAL、RHI、Engineがあり、Engine起動時にWindows Platform実装をD3D12 Backendへ直接注入していました。

### Legacy Strengths

- OS処理とRendering処理を名前上のModuleへ分離していた
- Backendを選択してEngineを構成する入口が存在した
- RHIを介してGraphics処理を抽象化する意図があった

### Legacy Problems

- Composition RootとModule契約の境界が明確でなく、Platform実装とBackend実装の具体型が起動処理へ集まりやすかった
- Module名だけでは公開API、Private実装、所有権、Thread Affinityを判断できなかった
- Platform固有型とGraphics API固有型がどの境界まで許可されるか明文化されていなかった
- 共通Moduleの責務が拡大し、上位Subsystemの機能が流入するRiskがあった

### Current Requirements

- RuntimeはEditorへ依存しない
- Platform非依存APIへWindows型を公開しない
- RHI APIへD3D12型を公開しない
- FoundationはPlatform、RHI、RuntimeHostへ依存しない
- 実装選択と所有権の組み立てを各最終ExecutableのComposition Rootへ限定する
- 公開HeaderとPrivate実装をSource配置とCMake Usage Requirementの両方で分離する
- Plugin ABIを先回りせず、将来の境界へSTL所有権やC++例外を公開しない

### New Design

Foundationを依存Graphの最下層とし、PlatformとRHIは相互に依存しない契約Targetとします。WindowsとD3D12は個別の実装Targetに隔離し、各最終ExecutableのComposition Rootだけが必要な実装を選択して所有します。M01からM05では`CueRuntimeHost`がその役割を担います。

### Validation

- CMake Target依存例をReviewし、循環がないことを確認する
- 後続Issue #33から#35のTarget、公開Header、Private Sourceの配置をこのADRだけから決定できることを確認する
- Foundationの公開Header単体CompileとLink入力検査をM01 Completion Gateへ追加する

## Decision

### Foundation Responsibilities

`Cue.Foundation`は、PlatformとRenderingに依存しないRuntime共通の最小機構だけを所有します。

所有する責務:

- 成功または失敗を表すValue Semanticsの`Result`とError情報
- Source Locationを含む診断Context
- Log Level、Log Record、Log SinkなどのPlatform非依存な診断契約
- AssertとFatal経路のPlatform非依存なPolicy
- Module間で共有する必要がある固定幅整数型など、標準C++だけで成立する基本契約

所有しない責務:

- Window、Message Pump、Input、Native Handle
- Filesystem、Process、Dynamic LibraryなどのOS Service
- RHI、Graphics Resource、Shader、GPU同期
- Runtime World、ECS、Asset、Serialization
- Editor、Tool、UI
- Application Frame LoopとSubsystemのComposition
- Global Registry、Service Locator、万能基底型
- Plugin ABIとDynamic Module Loading

Foundationへ機能を追加するには、PlatformとRHIの両方から利用するという理由だけでなく、標準C++だけで一貫した契約を定義できることを要求します。

### CMake Targets

Target名は責務を表す`Cue.<Module>`形式を使用し、PlatformまたはBackend実装は`Cue.<Module>.<Implementation>`形式を使用します。実行Targetは用途を表すPascalCase名を使用します。

| Target | Kind | Responsibility |
| --- | --- | --- |
| `Cue.Foundation` | Static Library | Platform非依存な基本契約と実装 |
| `Cue.Foundation.Windows` | Static Library | Windows UTF 変換の低層 Primitive。詳細は ADR-0009 |
| `Cue.Platform` | Static Library | Platform非依存なPlatform契約 |
| `Cue.Platform.Windows` | Static Library | Windows Windowing実装とPlatform固有Error変換。UTF PrimitiveはADR-0009 |
| `Cue.RHI` | Static Library | Graphics API非依存なRHI契約 |
| `Cue.RHI.D3D12` | Static Library | D3D12固有実装と変換境界 |
| `CueRuntimeHost` | Executable | 実装選択、所有権構築、Lifecycle制御 |

この表はTarget名と依存方向を決めるものであり、M01では`Cue.Foundation`以外を実装しません。PlatformとRHIのAPI詳細は各Research Issueで決定します。

Target種別の採用方針:

- Production Moduleは、独立したCompile、Usage Requirement、Link入力を検査できるStatic Libraryを基本とする
- Interface Libraryは、SourceとRuntime状態を持たず、HeaderまたはBuild設定だけを集約する必要が確認された場合に限定する
- Object Libraryは、消費側ごとに同じObjectを直接取り込む必要がある内部Build構造に限定し、公開Module境界として使用しない
- Shared LibraryはPlugin ABI、Allocation、Exception、Versioning方針が決まるまで採用しない

### Source Placement

Engine所有Sourceは次の規則で配置します。

```text
Engine/Source/<Module>/
    CMakeLists.txt
    Public/Cue/<Module>/
        <PublicHeader>.h
    Private/
        <PrivateHeader>.h
        <Source>.cpp
```

- `<Module>`とFile名はPascalCaseとする
- Public Headerは`#include <Cue/<Module>/<Header>.h>`でIncludeする
- Public Include Directoryは`Public`だけを`PUBLIC` Usage Requirementとして公開する
- `Private`はTarget自身だけのInclude Directoryとし、他TargetからIncludeしない
- Platform実装は`Engine/Source/Platform/Windows`、RHI Backendは`Engine/Source/RHI/D3D12`のように契約Module配下へ配置する
- Testは`Engine/Tests/<Module>`へ配置し、Production Targetの`Private`へ依存しない
- Repository RootのCMakeはSubsystemを列挙する入口に限定し、個別Source一覧とCompiler設定は所有TargetのCMakeへ置く

### Dependency Direction

許可する依存Graphは次のとおりです。

```text
Cue.Foundation
    ^
    +-- Cue.Foundation.Windows
    |       ^
    |       +-- Cue.Platform.Windows (Private)
    |       +-- Cue.RHI.D3D12 (Private)
    |
    +-- Cue.Platform <--- Cue.Platform.Windows
    |
    +-- Cue.RHI <-------- Cue.RHI.D3D12
    |
    +-- CueRuntimeHost

CueRuntimeHost
    +-- Cue.Platform
    +-- Cue.Platform.Windows
    +-- Cue.RHI
    +-- Cue.RHI.D3D12
```

矢印の始点は依存するTarget、終点は依存されるTargetを意味します。

依存規則:

- `Cue.Foundation`は他のEngine Moduleへ依存しない
- `Cue.Platform`と`Cue.RHI`は相互に依存しない
- Platform実装は`Cue.Platform`へ、RHI Backendは`Cue.RHI`へ実装依存を持つ。Windows UTF 変換に限り、ADR-0009 に従って両実装 Target から`Cue.Foundation.Windows`へ Private 依存できる
- RHI BackendはWindow実装やNative Window型を直接要求しない。必要な連携契約はPlatform非依存な値または後続ADRで定義する明示的なBoundaryへ置く
- RuntimeHostはComposition Rootとして具体実装を選択できるが、具体型を他Moduleの公開APIへ渡さない
- `CueRuntimeHost`は最終Executableであり、他TargetからLinkしない
- Editor、Tool、Game Runtimeなど将来のExecutableは、それぞれを独立したComposition Rootとし、必要な契約と実装Targetへ直接依存する
- 複数Executableで再利用するLifecycle処理が確認された場合は、Executableへ置かず、責務を定義したStatic Libraryへ分離する
- Foundationから上位Targetへの逆依存を作らない

CMakeでは`target_link_libraries`の`PUBLIC`、`PRIVATE`を使用し、Headerに現れる依存だけを`PUBLIC`とします。依存の便宜だけを理由にTransitive Dependencyを公開しません。

### Public and Private API Boundary

Public Headerへ置けるもの:

- Module利用者がCompileするために必要な型、関数、定数
- 所有権、Lifetime、Thread Affinity、失敗時状態をDoxygenで説明したAPI
- Platform非依存かつBackend非依存なValue Typeと非所有View
- 実装選択を隠すFactoryの宣言

Privateへ置くもの:

- Windows SDK、DXGI、D3D12をIncludeする型と関数
- Concrete Class、Native Handle、COM Object
- Module内部の同期Primitive、Cache、Allocator
- 診断SinkのPlatform固有実装
- 外部Library Adapter

Public Headerでは`Windows.h`、`wrl.h`、`dxgi*.h`、`d3d12.h`を直接または間接にIncludeしません。`HWND`、`HANDLE`、`HRESULT`、COM Interface、D3D12 EnumやDescriptorを公開型、引数、戻り値、Template引数へ使用しません。

PlatformまたはBackend固有の失敗は、その実装境界でFoundationのErrorへ変換します。Native Error値を診断用に保持する場合も、公開契約はNative Domain名と整数値の組として表し、Native Headerを要求しません。

### Ownership, Lifetime, and Thread Affinity

公開APIは次の共通規則に従います。

| API Category | Ownership | Lifetime | Thread Affinity |
| --- | --- | --- | --- |
| Foundation Value | 呼び出し側がValueとして所有 | 値自身のLifetime | Immutable操作は任意Thread。可変操作は外部同期 |
| Factory Result | 成功時に呼び出し側へ一意所有権を移譲 | Ownerの破棄まで | FactoryごとにDoxygenへ記述 |
| Non-owning View | 所有権を持たない | 呼び出し中だけ有効をDefaultとし、例外は明記 | 参照先のThread規則を継承 |
| Sink／Callback | 登録側がCallback OwnerとRegistration Handleを所有し、登録先は非所有参照 | Registration Handleの破棄または明示解除まで。登録先とCallback Ownerの両方がHandleより長く生存する | 呼出Threadと同期要件を登録APIへ記述 |
| Platform Object | RuntimeHostまたは上位Ownerが一意所有 | Native Resourceより先にOwnerを破棄しない | 生成Threadを既定とし、詳細はPlatform ADRで決定 |
| RHI Object | RuntimeHostまたはRHI Ownerが一意所有 | Parent DeviceとQueueの規則に従う | 詳細はRHI ADRで決定 |

- 所有権を持つRaw Pointerを公開しない
- 共有所有は要件が確認されるまで導入しない
- 破棄順序はOwnerの型とFactory Resultで表し、Global Shutdown順序へ依存しない
- Thread Safeと記載しないAPIはThread Safeとみなさない
- Callbackは呼出Thread、再入可能性、登録解除との競合を明記する
- Callback登録はMove-onlyなRAII Registration Handleを返し、その解除完了後は登録先からCallbackが呼ばれないことを保証する
- Callback OwnerはRegistration Handleより長く生存させる。Registration HandleはCallback Ownerの破棄前に明示解除または破棄する
- 登録先はすべてのRegistration Handleより長く生存させる。Composition OwnerはHandle、Callback Owner、登録先の順に破棄する
- 登録先の破棄時に有効なRegistration Handleが残る設計を許可しない。安全な失効機構を別ADRで決定した場合だけ例外とする

### ABI Boundary

M01のTargetは同一Repository、同一Toolchain、同一Runtime LibraryでBuildするFirst-party Static Libraryです。この境界は安定ABIではありません。

将来のDLLまたはPlugin境界では、このPublic C++ APIをそのまま公開しません。Version付きの別ABIを定義し、STL所有権、C++例外、生Pointer所有権を越境させないAdapterを設けます。

## CMake Dependency Example

後続Issueは次の形で依存方向を表現します。

```cmake
add_subdirectory(Engine/Source/Foundation)

# Future milestones
# add_subdirectory(Engine/Source/Platform)
# add_subdirectory(Engine/Source/RHI)
# add_subdirectory(Engine/Source/RuntimeHost)
```

Module側では、利用する最小依存だけを宣言します。

```cmake
target_link_libraries(Cue.Platform PUBLIC Cue.Foundation)
target_link_libraries(Cue.Foundation.Windows PUBLIC Cue.Foundation)
target_link_libraries(Cue.Platform.Windows PUBLIC Cue.Platform PRIVATE Cue.Foundation.Windows)
target_link_libraries(Cue.RHI PUBLIC Cue.Foundation)
target_link_libraries(Cue.RHI.D3D12 PUBLIC Cue.RHI PRIVATE Cue.Foundation.Windows)

target_link_libraries(
    CueRuntimeHost
    PRIVATE
        Cue.Foundation
        Cue.Platform
        Cue.Platform.Windows
        Cue.RHI
        Cue.RHI.D3D12
)
```

このGraphに逆向きの依存はなく、循環はありません。実際のTarget追加時にはCMake Graphviz出力またはTarget Link入力をCompletion Gateで確認します。

## Consequences

### Positive

- WindowsとD3D12のHeaderをFoundationと契約Targetから排除できる
- PlatformとRHIを独立してTestできる
- 各ExecutableのComposition Root以外のModuleが具体実装の組み合わせを知る必要がない
- Source配置とCMake Usage Requirementから公開境界を判断できる
- Moduleごとの所有権、Lifetime、Thread Affinityの記述漏れをReviewできる

### Negative

- Static Libraryが増え、TargetごとのCMake定義と依存管理が必要になる
- 実装Targetと契約Targetの間に変換Codeが必要になる
- ABI安定性は提供せず、Plugin化には別の設計とAdapterが必要になる
- PlatformとRHIの連携値は後続Research Issueで追加設計が必要になる

## Enforcement

- 新しいEngine Targetはこの依存方向と配置規則に従う
- Public HeaderがPlatformまたはBackend HeaderをIncludeした場合はBuildまたはReviewで失敗扱いにする
- Target循環と不要なTransitive DependencyをM01 Completion Gateで検査する
- 規則を変更する場合は、このADRをSupersedeするADRを作成する

## Follow-up

- Issue #32でError、Assert、Log方針を決定する
- Issue #33で`Cue.Foundation`と基本Result型を実装する
- Issue #34で診断LogとAssert経路を実装する
- Issue #35で公開Header単体Compile、Test、依存方向のCompletion Gateを追加する
