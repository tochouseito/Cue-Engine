# ADR-0007: Minimum RHI and D3D12 Device Boundary

- Status: Accepted
- Date: 2026-08-22
- Decision Owners: CueEngine Project

## Context

M03では、Render Target Clearへ進む前に、Graphics API非依存なRHI契約とD3D12固有実装の最小境界を確立する必要がある。Device生成、Adapter選択、診断、将来のSwap Chain連携を一つのBackendへ無制限に集約すると、Platform固有型、D3D12型、所有権、Threading、失敗時処理がRuntimeへ漏れるRiskがある。

このADRは、M03からM05で必要となるRHI公開API、D3D12 Private実装、所有権、Shutdown、Native Window連携、Thread Affinity、Feature Level、Adapter Policy、Device Removal診断、Capability Reportを決定する。Texture、Buffer、Shader、Pipeline、General Resource Manager、FrameGraph、Multi-API実装は決定しない。

## Legacy Reference

### Legacy Problem

旧CueEngineも、Platform WindowからD3D12 Device、Command Queue、Swap Chain、Render Targetを構築し、Frameを描画して終了時に解放する必要があった。

### Legacy Approach

旧`D3D12Backend`はDevice生成直後にDescriptor、Buffer、Texture、View、Pipeline、Command、Queue、Swap Chainに関係するManagerをまとめて生成し、Windows Platform実装からNative Window Handleを取得した。

### Legacy Strengths

- Graphics初期化の入口が一箇所にまとまっていた
- BackendからClearに必要なD3D12 Objectへ到達できた
- WindowとSwap Chainを短い経路で接続できた

### Legacy Problems

- Clearに不要なManagerまで初期化成功の前提になった
- Device、Queue、Swap Chain、Frame ResourceのOwnerとShutdown順序をAPIから判断できなかった
- RuntimeHostとBackendがWindows具体型を知り、PlatformとRHIの境界が密結合した
- Debug Layer、DRED、InfoQueue、Device RemovalのPolicyがBackend Lifecycleから独立していなかった
- Capability不足とNative API失敗の区別が上位へ伝播しにくかった

### Current Requirements

- `Cue.RHI`の公開HeaderへWindows、DXGI、D3D12型を出さない
- `Cue.RHI`と`Cue.Platform`を相互依存させない
- RuntimeHostをComposition Rootとし、Graphics Backendを一意所有する
- M03では診断、Adapter選択、Device生成だけを実装する
- M04以降のQueue、Swap Chain、Frame Contextを先回りして生成しない
- Graphics API固有失敗をFoundationの`Result`と`Error`へ変換する
- 初期実装はWindows x64、D3D12、従来Graphics Pipelineを対象とする

### New Design

`Cue.RHI`はPlatform非依存なBackend契約とCapability値だけを所有し、`Cue.RHI.D3D12`はDXGI Factory、Adapter、D3D12 Device、診断状態をPrivateに所有する。RuntimeHostはD3D12 Factoryから返されたBackendを一意所有するが、Native Deviceへアクセスしない。

WindowとSwap Chainの接続は、M04で追加する専用Adapter Target `Cue.RHI.D3D12.Windows`だけが行う。このTargetは`Cue.Platform.Windows`と`Cue.RHI.D3D12`の両方を利用できるが、どちらからも依存されない。RuntimeHostはWindowとD3D12 BackendをAdapter Factoryへ渡し、`HWND`、DXGI、D3D12型を受け取らない。

`Cue.RHI.D3D12`はPlatform型を知らない`D3d12Backend`契約を公開する。M04では、この契約へWindows Presentation Adapterだけが呼べるPrivateなOpaque Window消費入口を追加する。Adapterは短命なNative Viewから得た`const void*`を同期呼出中だけ渡し、D3D12 Backendが内部でSwap Chainを生成する。D3D12 DeviceまたはQueueをAdapter Targetへ公開しない。

### Validation

- Device生成、Swap Chain生成、Clear、ShutdownのSequenceをReviewする
- API SketchにWindows、DXGI、D3D12型が現れないことを確認する
- Target依存にPlatformとRHIの循環がないことを確認する
- 後続Issue #42から#45を、このADRのScopeを拡大せず実装できることを確認する

## Decision

### Module and Target Boundary

M03では次のTargetを使用する。

| Target | Responsibility | Allowed Dependencies |
| --- | --- | --- |
| `Cue.RHI` | Backend契約、Configuration、Capability値 | `Cue.Foundation` |
| `Cue.RHI.D3D12` | D3D12診断、DXGI Adapter選択、Device生成 | `Cue.RHI`、`Cue.Foundation`、DXGI、D3D12、DXGUID |
| `CueRuntimeHost` | 実装選択、Owner構築、Errorの最終Log | Foundation、Platform、RHIの契約と選択実装 |

M04でSwap Chainが必要になった時点で、次のAdapter Targetを追加する。

| Target | Responsibility | Allowed Dependencies |
| --- | --- | --- |
| `Cue.RHI.D3D12.Windows` | Windows WindowとD3D12 Presentationの限定連携 | `Cue.Platform.Windows`、`Cue.RHI.D3D12`、DXGI、D3D12 |

依存規則:

- `Cue.RHI`はFoundation以外のEngine Targetへ依存しない
- `Cue.RHI.D3D12`は`Cue.Platform`または`Cue.Platform.Windows`へ依存しない
- `Cue.Platform`と`Cue.Platform.Windows`はRHIへ依存しない
- `Cue.RHI.D3D12.Windows`はComposition Adapterであり、Platform契約またはRHI契約へ逆依存を作らない
- DXGI、D3D12、DXGUIDのLink入力はD3D12実装Targetへ`PRIVATE`で指定する
- Windows、DXGI、D3D12 HeaderはD3D12実装またはWindows AdapterのPrivate SourceだけでIncludeする
- M03では`Cue.RHI.D3D12.Windows`を追加せず、Native Window HandleをGraphics Backendへ渡さない

### Public RHI Boundary

`Cue.RHI`の公開APIは次だけを提供する。

- Graphics Backendの一意所有契約
- Backend生成時のPlatform非依存設定
- AdapterとDeviceから変換された安定Capability値
- Backend種別を識別する値
- Backend LifecycleとThread AffinityのDocumentation

`Cue.RHI`の公開APIは次を提供しない。

- Native Device、Adapter、Queue、Swap Chain、Descriptor Handle
- Texture、Buffer、Shader、Pipeline、Command Recording API
- Native Window HandleまたはPlatform Window API
- Backend内部Manager、Global Registry、Service Locator
- Device Recovery、Adapter Hot Plug、Multi-GPU

DXGIとD3D12固有のConfiguration名は`Cue.RHI.D3D12`の公開Factory Headerへ限定できる。ただし、そのHeaderにもWindows SDK型、COM Interface、`HRESULT`、D3D Feature Level定数を出さない。

### Ownership and Lifetime

| Object | Owner | Lifetime Rule |
| --- | --- | --- |
| Graphics Backend | RuntimeHostまたは将来のComposition Root | Logger、Fatal Handler、Assert Contextより先に破棄する |
| DXGI Factory | D3D12 Backend | Adapterより長く生存する |
| DXGI Adapter | D3D12 Backend | Device生成後もCapabilityとDevice Removal診断に必要な間だけ保持する |
| D3D12 Device | D3D12 Backend | Queue、Frame Context、Swap Chainより長く生存する |
| Graphics Queue | D3D12 Backend | M04で追加し、Frame ContextとSwap Chainより長く生存する |
| Presentation Context | Composition Root | M04で追加し、BackendとWindowの両方より先に破棄する |
| Swap Chain | Presentation Context | Window破棄開始前にGPU利用を停止して破棄する |
| Frame Context | Presentation Context | 各Frame Fence完了後に再利用し、Swap Chainより先に破棄する |

- Factoryは`Result<std::unique_ptr<GraphicsBackend>>`で一意所有権を移譲する
- 所有権を持つRaw Pointer、共有所有、Global Device Singletonを公開しない
- Raw PointerまたはReferenceを使用する場合は呼出中だけ有効な非所有参照とする
- Backendは注入された`Logger`、`FatalHandler`、`AssertContext`を所有しない
- 初期化途中の失敗では、生成済みNative Objectだけを逆順に解放してErrorを返す
- Backend破棄は例外を投げず、Device Removal診断を除いて新しいGraphics処理を開始しない

Backend状態は`Ready`、`DeviceRemoved`、`Shutdown`の三つとする。

- 正常経路は`Ready -> Shutdown`、Device Removal経路は`Ready -> DeviceRemoved -> Shutdown`だけを許可する
- `DeviceRemoved`後のGPU操作は新しいWorkを開始せず、`RHI.BackendUnavailable` Errorを返す
- `capabilities()`と`state()`はBackend Objectの破棄まで参照できる
- `shutdown()`は明示的に呼び、成功後の再呼出は成功する冪等操作とする
- `shutdown()`開始後は新しいWorkを拒否し、途中で失敗してもBest-effort Cleanupを最後まで行って必ず`Shutdown`へ遷移し、最初のErrorを返す。完了後の再呼出は直前の成否にかかわらず成功する
- Queue追加後の通常`shutdown()`は最後のFenceをSignalし、RuntimeHostが指定する有限時間だけ待つ。M04のQueue ADRで既定時間と待機Primitiveを決定する
- Fence Signal、Wait、Device Removal診断の失敗は`shutdown()`の`Result`で返し、RuntimeHostが一度Logして終了Codeを決定する
- `DeviceRemoved`ではFence完了を待たず、DRED収集後にNative Objectを解放する
- RuntimeHostはBackend Destructorより前に`shutdown()`を呼ぶ。Destructorは未解放Native Objectを例外なしで解放する最後の安全網であり、失敗を隠す通常経路として使用しない

`std::unique_ptr`はAccepted ADR-0004で定めた同一Repository、同一ToolchainのFirst-party Static Library境界でのみ使用する。これは安定ABIではない。将来のDLLまたはPlugin境界では、このFactoryを直接公開せず、Version付きABI AdapterでSTL所有権、C++例外、Raw Ownershipを越境させない。

### Native Window Boundary

- `Cue.RHI`と`Cue.RHI.D3D12`は`NativeWindowView`を受け取らない
- RuntimeHostは`NativeWindowView::value()`をGraphics Backendへ転送しない
- M03のDevice生成はWindowなしで完了できる
- M04の`Cue.RHI.D3D12.Windows`だけが`get_native_window_view()`を呼び、取得直後のSwap Chain生成中だけOpaque Handleを参照する
- Adapter Factoryは`D3d12Backend&`と`Window&`を受け、Native Handleを公開Factoryの引数または戻り値へ出さない
- `D3d12Backend`のPrivate Presentation入口は`D3d12WindowsPresentationAccess`だけをFriendとし、Opaque Window値を呼出中に同期消費する
- Presentation AdapterはNative Device、Queue、DXGI Factoryを取得せず、D3D12 BackendのPrivate操作を呼ぶ
- D3D12 BackendはOpaque Window値を保存せず、Swap Chain生成完了前に参照を終了する
- Adapter Factoryへ渡すWindowは`create_windows_window_system()`由来に限定し、異なる実装はUnchecked Castせず診断可能なErrorにする
- AdapterはWindowまたはBackendを所有せず、成功時にPresentation Contextの一意所有権だけを返す
- Presentation ContextはWindowの`destroy()`開始前に破棄する
- Native Handleを保存、破棄、Window同一性、Resource IDとして使用しない

この限定AdapterはADR-0006の`NativeWindowView`利用規則を維持し、PlatformとRHIのCore Target間へ依存を追加しない。

### Threading Model

M03からM05の初期Graphics Threadは、RuntimeHostのMain ThreadかつWindow Threadと同一とする。

- Backend生成、Capability参照、Device生成、Queue操作、Presentation、Frame Context操作、Shutdownは生成Thread上で行う
- RHI ObjectはThread Safeとしない
- Backendは内部Worker Threadを生成しない
- Loggerだけが既存契約どおりThread Safeであり、RHI ObjectのThread Safetyを意味しない
- D3D12が複数ThreadからのQueue Submitを許可しても、初期RHI契約では許可しない
- 将来のParallel Command Recordingは、Command Allocator、Command List、Fence、Frame Slotの所有権を別ADRで決定してから追加する

Thread違反はProgrammer ErrorとしてAssert対象とする。Thread IDを永続形式または公開Capabilityへ保存しない。

### D3D12 and Windows Requirements

- 最小D3D Feature Levelは`12_0`とする
- Adapter ProbeとDevice生成は同じFeature Level `12_0`を要求する
- `12_1`または`12_2`を必須条件にせず、Mesh Shaderを前提にしない
- Feature Level `11_0`または`11_1`への自動Downgradeは行わない
- Windows SDKはM00で検証済みの`10.0.26100.0`以上をBuild要件とし、Issue #42でConfigure時に検査する
- 最小Runtime OSはWindows 10 version 1903とする
- SDKに存在してもRuntimeで利用できない診断InterfaceはQuery失敗を検出して安全にFallbackする

Feature Levelは機能条件であり性能順位ではない。Optional Capabilityは必要となるIssueで`CheckFeatureSupport`を使用して個別に確認する。

### Adapter and WARP Policy

Hardware経路:

- DefaultはHigh Performance Hardware Adapterを優先する
- `EnumAdapterByGpuPreference`の順序で候補を評価する
- Software AdapterをHardware候補から除外する
- 各候補をFeature Level `12_0`でProbeし、未対応候補は診断情報を保持して次へ進む
- 最初の列挙Adapterという理由だけで選択しない
- 対応Hardware Adapterがなければ診断可能なErrorを返す

WARP経路:

- WARPは明示ConfigurationまたはTest Modeでだけ選択する
- Hardware失敗時にWARPへ暗黙Fallbackしない
- WARPもFeature Level `12_0`を満たさなければErrorを返す
- WARP成功をHardware対応のEvidenceとして扱わない

この方針により、製品実行時のHardware不足を隠さず、CIや診断では再現可能なSoftware Adapter経路を利用できる。

### Diagnostics and Device Removal

診断設定はBackend生成前に確定し、次の順序で適用する。

```text
Resolve Configuration
    -> Enable DRED settings when requested and available
    -> Enable Debug Layer when requested and available
    -> Enable optional GPU Based Validation only when explicitly requested
    -> Create DXGI Factory with matching debug flag
    -> Select Adapter
    -> Create Device
    -> Configure InfoQueue
```

- DebugとDevelopmentはDebug Layer、DRED、InfoQueueを有効化する既定とする
- ReleaseはDebug Layer、GPU Based Validation、InfoQueue Callbackを無効化する
- DREDのRuntime利用可能性を確認し、利用不可をDevice生成失敗にしない
- GPU Based Validationは高Costのため既定で無効とし、明示Optionでだけ有効化する
- Debug Layerが利用不可ならWarningを一度記録し、診断なしのDevice生成へFallbackする
- 既知Messageを無差別にSuppressせず、BreakとFilterはMessage Severityと確認済みIDに限定する
- Native Failureは`HRESULT`を符号付き64-bit値へ変換し、Domain `D3D12`または`DXGI`の`NativeError`として保持する
- Backend内部は同じErrorを重複Logせず、RuntimeHostが上位Context付きで一度だけLogする

Configuration既定と明示Overrideは次のとおりとする。

| Build | Default Validation | Default DRED | Explicit Override |
| --- | --- | --- | --- |
| Debug | Standard | Enabled | Disabled、Standard、GpuBasedを許可する |
| Development | Standard | Enabled | Disabled、Standard、GpuBasedを許可する |
| Release | Disabled | Disabled | Standard、GpuBased、DRED有効化はInvalid Configuration Errorにする |

Compile Configurationごとの既定値はRuntimeHostのComposition Rootが解決し、Backend Factoryは受け取ったDescriptorを検証する。BackendがReleaseの禁止設定を暗黙に無視または変更しない。

Device RemovedまたはDevice Resetを検出した場合:

1. 新しいGPU Workの受付を停止する
2. `GetDeviceRemovedReason`のNative Errorを取得する
3. 利用可能ならDRED Auto BreadcrumbとPage Fault情報を同期的に収集してLogする
4. 無制限なFence Waitを行わず、所有Objectを逆順に解放する
5. Recoverableな起動／Frame失敗として上位へErrorを返し、Process継続可否はComposition Rootが判断する

M03からM05ではDevice Recovery、Resource再生成、Adapter再選択、Telemetry Uploadを実装しない。

### Capability Report

`CapabilityReport`はBackendが所有するNative Objectを公開せず、次のPlatform非依存Valueだけを持つ。

| Field | Meaning |
| --- | --- |
| `backendKind` | 選択Backendの識別子 |
| `adapterKind` | HardwareまたはSoftware |
| `adapterName` | UTF-8へ変換した診断名 |
| `vendorId` | Adapter Vendor ID |
| `deviceId` | Adapter Device ID |
| `dedicatedVideoMemoryBytes` | Dedicated Video MemoryのByte数 |
| `isUma` | Unified Memory Architectureか |
| `profile` | CueEngineが要求するPlatform非依存Capability Profile |

初期`GraphicsProfile`は`Baseline3D`だけとし、D3D12 Feature Level `12_0`へ対応付ける。Native Feature Level名はD3D12診断Logへ記録するが、D3D定数を`CapabilityReport`へ格納しない。

- ReportはBackend生成成功後からBackend破棄開始までImmutableとする
- Adapter名以外の文字列化済みD3D12情報を安定契約へ追加しない
- Shader Model、Resource Binding Tier、Mesh Shader Tierなど、M03で利用しないOptional Featureを追加しない
- Capability追加は実際に分岐または診断で使用するIssueに限定する

### API Sketch

`Cue.RHI`の公開契約:

```cpp
namespace cue
{
enum class GraphicsBackendKind
{
    D3d12,
};

enum class GraphicsAdapterKind
{
    Hardware,
    Software,
};

enum class GraphicsProfile
{
    Baseline3D,
};

enum class GraphicsBackendState
{
    Ready,
    DeviceRemoved,
    Shutdown,
};

struct CapabilityReport final
{
    std::string adapterName;
    std::uint64_t dedicatedVideoMemoryBytes;
    std::uint32_t vendorId;
    std::uint32_t deviceId;
    GraphicsBackendKind backendKind;
    GraphicsAdapterKind adapterKind;
    GraphicsProfile profile;
    bool isUma;
};

class GraphicsBackend
{
  public:
    virtual ~GraphicsBackend() = default;

    GraphicsBackend(const GraphicsBackend &) = delete;
    GraphicsBackend &operator=(const GraphicsBackend &) = delete;

    [[nodiscard]] virtual const CapabilityReport &capabilities() const noexcept = 0;
    [[nodiscard]] virtual GraphicsBackendState state() const noexcept = 0;
    [[nodiscard]] virtual Result<void> shutdown() noexcept = 0;

  protected:
    GraphicsBackend() = default;
};
} // namespace cue
```

`Cue.RHI.D3D12`の公開Factory契約:

```cpp
namespace cue
{
enum class D3d12AdapterPolicy
{
    HighPerformanceHardware,
    Warp,
};

enum class D3d12ValidationMode
{
    Disabled,
    Standard,
    GpuBased,
};

struct D3d12BackendDescriptor final
{
    D3d12AdapterPolicy adapterPolicy;
    D3d12ValidationMode validationMode;
    bool isDredEnabled;
};

class D3d12WindowsPresentationAccess;
class PresentationContext;

class D3d12Backend : public GraphicsBackend
{
  private:
    friend class D3d12WindowsPresentationAccess;

    [[nodiscard]] virtual Result<std::unique_ptr<PresentationContext>> create_windows_presentation(
        const void *a_window, AssertContext &a_assertContext) noexcept = 0;
};

[[nodiscard]] Result<std::unique_ptr<D3d12Backend>> create_d3d12_backend(
    const D3d12BackendDescriptor &a_descriptor, AssertContext &a_assertContext) noexcept;
} // namespace cue
```

`GraphicsBackendState`は`Ready`、`DeviceRemoved`、`Shutdown`を表す。`D3d12Backend`のPrivate Presentation入口と`PresentationContext`はM04で追加し、M03 Production APIへ先回りして実装しない。Friend AccessはWindows Adapterだけに限定し、Native Device、Queue、Factoryを返さない。

命名、Field順、Configuration既定値は後続実装IssueでCoding Rulesと構築順に合わせて調整できる。ただし、責務、所有権、Platform非依存性、Scopeは維持する。

### Lifecycle Sequences

Device生成:

```text
RuntimeHost          Cue.RHI.D3D12              DXGI / D3D12
    | create backend       |                          |
    |--------------------->| configure diagnostics   |
    |                      | create DXGI factory ---->|
    |                      | enumerate/probe -------->|
    |                      | create device ---------->|
    |                      | configure InfoQueue ---->|
    |<---------------------| Backend + Capability     |
    | owns Backend         | owns Factory/Adapter/Device
```

M04以降のSwap Chain生成とClear:

```text
RuntimeHost      Windows Adapter       Platform Window      Presentation Context      D3D12 Backend
    | Window + Backend |                     |                      |                       |
    |----------------->| borrow native view  |                      |                       |
    |                  |-------------------->|                      |                       |
    |                  | create presentation through friend access ---------------------->|
    |<-----------------| owns Presentation Context                |                       |
    | begin frame ----------------------------------------------->| acquire / record ---->|
    | clear RTV -------------------------------------------------->| clear --------------->|
    | present ---------------------------------------------------->| present ------------->|
    | end frame -------------------------------------------------->| signal fence -------->|
```

通常Shutdown:

```text
Stop accepting frames
    -> signal and wait for last submitted graphics fence
    -> destroy Frame Contexts
    -> destroy Swap Chain and Presentation Context
    -> destroy Graphics Queue
    -> destroy D3D12 Device
    -> release Adapter
    -> release DXGI Factory and diagnostic interfaces
    -> destroy Window
    -> destroy Window System
    -> flush Logger
```

Device Removed時はFence完了を無制限に待たず、Removal ReasonとDREDを収集してから同じ所有順序で破棄する。

## Consequences

### Positive

- RHI公開APIとRuntimeHostからWindows、DXGI、D3D12型を排除できる
- Deviceだけを生成するM03と、Queue、Swap Chain、Clearを追加するM04、M05を段階的に実装できる
- PlatformとRHI Coreの独立性を維持しながらNative Window連携を限定できる
- Hardware不足と明示WARP Testを区別できる
- Device Removal時の診断入口と非復旧Scopeを固定できる

### Negative

- WindowsとD3D12の組み合わせごとにPresentation Adapter Targetが必要になる
- 初期Threading ModelはSingle Threadであり、Parallel Command Recordingへ別設計が必要になる
- Feature Level `12_0`未対応Hardwareを対象外とする
- `std::unique_ptr` FactoryはFirst-party Static Library境界専用であり、Plugin ABIへ直接公開できない

## Enforcement

- `Cue.RHI`公開Header単体Compile TestでWindows、DXGI、D3D12 Header非依存を検査する
- CMake Target GraphとLink入力でPlatform、RHI、D3D12依存方向を検査する
- D3D12、DXGI、DXGUIDがD3D12実装Target以外へ伝播した場合は失敗扱いにする
- RuntimeHostがWindows SDK HeaderまたはNative Window ViewをGraphics Backendへ渡した場合は失敗扱いにする
- HardwareとWARPの選択経路を独立してTestする
- Debug、Development、ReleaseでDevice SmokeとShutdownを検証する

## Sources

- [Microsoft: Hardware Feature Levels](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels)
- [Microsoft: Capability Querying](https://learn.microsoft.com/en-us/windows/win32/direct3d12/capability-querying)
- [Microsoft: IDXGIFactory6::EnumAdapterByGpuPreference](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference)
- [Microsoft: ID3D12Debug::EnableDebugLayer](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/nf-d3d12sdklayers-id3d12debug-enabledebuglayer)
- [Microsoft: GPU-based Validation and the Direct3D 12 Debug Layer](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation)
- [Microsoft: Use DRED to Diagnose GPU Faults](https://learn.microsoft.com/en-us/windows/win32/direct3d12/use-dred)
- [Microsoft: Design Philosophy of Command Queues and Command Lists](https://learn.microsoft.com/en-us/windows/win32/direct3d12/design-philosophy-of-command-queues-and-command-lists)

## Follow-up

- Issue #42で`Cue.RHI`と`Cue.RHI.D3D12` Target、公開Header Compile Test、依存方向Gateを追加する
- Issue #43でDebug Layer、DRED、InfoQueue診断を実装する
- Issue #44でHardware Adapter選択、WARP経路、Capability変換を実装する
- Issue #45でD3D12 Device生成、RuntimeHost Device Smoke、M03 Completion Gateを追加する
- M04のResearch Issueで`Cue.RHI.D3D12.Windows`とPresentation Contextの詳細APIを、このADRのNative Window境界内で決定する
