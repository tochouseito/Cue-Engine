# ADR-0006: Platform Window Boundary and Native Handle Policy

- Status: Accepted
- Date: 2026-08-22
- Decision Owners: CueEngine Project

## Context

M02では、Renderingより先にRuntimeHostから利用できる最小Window Lifecycleを確立する必要がある。Win32の型、Message、Thread規則をそのままRuntimeへ公開すると、Platform契約とWindows実装の境界が失われ、将来のRHI連携も`HWND`へ直接依存する。

このADRは、Window Ownership、Lifecycle、Thread Affinity、Event、Native Handle、UTF変換、Message Pump、初期Window数、失敗時Cleanupを決定する。Input、D3D12 Swap Chain、Editor Multi-windowは決定しない。

## Legacy Reference

### Legacy Problem

旧CueEngineも、Win32 Windowの生成、Message処理、終了通知、D3D12 Swap Chainへ渡すNative Handleを扱う必要があった。

### Legacy Approach

旧`WinApp`は`HWND`を所有し、Window Class登録、Message Handler、Input、Drag and Dropまで担当した。D3D12 BackendはWindows Platform実装から`HWND`を直接取得した。

### Legacy Strengths

- Window生成からMessage処理までの入口が一箇所にまとまっていた
- D3D12 Backendが必要な`HWND`を取得できた
- Win32機能を短い経路で追加できた

### Legacy Problems

- Window、Input、Drag and Dropの責務が一つの型へ集中した
- RuntimeHostとD3D12 BackendがWindows具体型を知る必要があった
- Close RequestとNative Resource破棄の境界が曖昧だった
- Native Handleの所有権と利用可能期間がAPIから判断できなかった
- Window Threadと他Threadの呼出制約が明示されなかった

### Current Requirements

- `Cue.Platform`の公開HeaderはWindows SDKを要求しない
- `Cue.Platform.Windows`だけがWin32型とUnicode Win32 APIを扱う
- RuntimeHostはPlatform契約を通してWindowを所有する
- Close Request、Destroyed、Resize、Minimize、Restoreを区別する
- Native Handleは将来のRHI連携など、明示的なInterop境界だけで一時参照する
- Recoverable ErrorはFoundationの`Result`で返し、Native Errorを診断Contextへ保持する
- 初期ScopeはWindows x64の単一Main Windowとする

### New Design

`Cue.Platform`はPlatform非依存のWindow契約とEvent値を所有し、`Cue.Platform.Windows`はその契約を実装する。RuntimeHostはWindow SystemとWindowを一意所有し、作成Thread上でMessage PumpとLifecycle操作を実行する。

Native Handleは通常のWindow操作APIから分離した`NativeWindowView`として取得する。Viewは非所有で、取得直後のWindows固有操作中だけ有効とし、保存を禁止する。RHI連携へこの型を転用することは禁止し、PlatformとRHIの双方が依存できるInterop境界は後続RHI Research Issueで決定する。

### Validation

- Create、Show、Resize、Minimize、Restore、Close、DestroyのSequenceをReviewする
- RuntimeHostがWin32型をIncludeせずLifecycleを完結できることをAPI Sketchで確認する
- 将来のRHI BackendがWindow Ownerにならず、`Cue.Platform.Windows`の型も直接受け取らないことを確認する
- 後続Issue #37から#40の配置、依存、Test境界をこのADRから判断できることを確認する

## Reference Engine Comparison

| Engine | Relevant Approach | Strengths | Trade-offs for CueEngine M02 |
| --- | --- | --- | --- |
| Unreal Engine | `FGenericWindow`がPlatform共通操作を持ち、`GetOSWindowHandle()`で`void*`を返す | Native実装を共通Window型の背後へ隠しつつ外部API連携が可能 | Native Handle取得が通常Window APIに常設される。M02では別Interop Headerへ分離し、利用範囲を狭める |
| Unity | `Screen`と`FullScreenMode`がPlayer向けの高水準Window設定を提供する | Game利用者がNative Window Lifecycleを意識せず扱える | PlayerとEngine内部の責務が同じAPI面に見えやすい。M02ではEngine内部契約だけを定義する |
| Godot | `DisplayServer`がWindow ID、Window Event、複数Window機能を集約する | 複数Platformと複数Windowを統一的に扱える | 初期M02には機能量が多く、Global Service化とID管理を先行させるRiskがある |
| CueEngine M02 | 一意所有の`WindowSystem`と`Window`、Owner-local Event Queue、分離したNative Interop View | 所有権、Thread、失敗、Interopの範囲を小さく保てる | 単一Window限定であり、Editor Multi-window追加時に契約を拡張する必要がある |

比較軸に対する判断:

| Axis | M02 Decision |
| --- | --- |
| Usability | RuntimeHostはDescriptor、`show()`、`pump_events()`、`try_pop_event()`、`destroy()`だけで基本Lifecycleを扱う |
| Runtime Performance | EventはOwner-local QueueへValueとして格納し、Callbackの再入と共有同期を避ける |
| Iteration Speed | Hidden Windowを使うLifecycle Testと自動終了可能なRuntimeHost Smoke Testを用意する |
| Extensibility | Native実装FactoryとInteropを分離し、Platform契約へWin32型を追加しない |
| Portability | UTF-8、固定幅Size、Platform非依存Enumだけを契約へ置く |
| Data Safety | WindowとSystemを一意所有し、Native Viewの保存と所有権移譲を禁止する |
| Diagnostics | Win32 Errorを`NativeError`へ変換し、RuntimeHost境界で必要なContextを追加してLogする |
| Testability | Event値のContract TestとWindows実Window Testを分け、Production APIへTest専用操作を追加しない |

## Decision

### Targets and Source Placement

```text
Engine/Source/Platform/
    CMakeLists.txt
    Public/Cue/Platform/
        Window.h
        WindowEvent.h
        WindowSystem.h
    Private/
        Window.cpp
    Windows/
        CMakeLists.txt
        Public/Cue/Platform/Windows/
            WindowsPlatform.h
            WindowsWindowInterop.h
        Private/
            WindowsWindow.cpp
            WindowsWindow.h
            UtfConversion.cpp
            UtfConversion.h
```

- `Cue.Platform`はWindow契約とPlatform非依存Value Typeを所有する
- `Cue.Platform.Windows`はWindows実装Factoryと明示的なInterop境界を所有する
- `Cue.Platform`は`Cue.Foundation`へ`PUBLIC`依存する
- `Cue.Platform.Windows`は`Cue.Platform`へ`PUBLIC`依存し、Windows SDK Libraryへ`PRIVATE`依存する
- `Cue.Platform`はRHI、D3D12、RuntimeHost、Editorへ依存しない
- `Cue.Platform.Windows`はRHIとD3D12へ依存しない

### Ownership and Lifetime

- RuntimeHostは`WindowSystem`を`std::unique_ptr`で一意所有する
- RuntimeHostは`FatalHandler`、`Logger`、`AssertContext`をこの順に構築し、`AssertContext`の非所有参照をWindows実装Factoryへ注入する
- `WindowSystem`はWin32 Window Class登録とWindow実装の生成を管理する
- `WindowSystem`は注入された`AssertContext`の非所有参照を保持し、Thread前提のAssertに使用する。Error生成とAllocation失敗では`AssertContext::fatal_handler()`を`EmergencyHandler`として使用する
- RuntimeHostは`Window`を`std::unique_ptr`で一意所有する
- `Window`はNative Window Handle、Window状態、Event Queueを所有する
- `WindowSystem`は自身から生成したすべての`Window`より長く生存する
- `AssertContext`、その`Logger`、`FatalHandler`は`WindowSystem`と、そこから生成したすべての`Window`より長く生存する
- `Window`の破棄完了後に`WindowSystem`を破棄し、最後のWindow破棄後にWindow Classを解除する
- Window、Event、Native Viewは共有所有しない
- Destructionは例外を送出しない。明示的な`destroy()`で失敗を返せる操作を完了し、`Window`と`WindowSystem`のDestructorはWindow Thread上でのみ実行して残存ResourceをBest-effort Cleanupする
- RuntimeHostは通常経路と初期化途中の失敗経路の両方で、Window、WindowSystem、AssertContext、Logger、FatalHandlerの順に破棄する

### Lifecycle State

Platform契約は次の状態を区別する。

```text
Created --show()--> Visible
   |                   |
   +----destroy()------+--> Destroyed
                       |
                 WM_CLOSE
                       v
                CloseRequested --destroy()--> Destroyed
```

- `Created`はNative Windowが存在するが、まだ表示されていない状態
- `Visible`は表示済みで通常のMessageを処理できる状態
- `CloseRequested`は利用者が終了判断を行う状態であり、Native Windowはまだ有効
- `Destroyed`はNative Handleが無効な終端状態である。`destroy()`だけは冪等な成功とし、その他のNative操作を許可しない
- `WM_CLOSE`を受信しただけでは`DestroyWindow`を呼ばず、Default Window Procedureへも渡さない。`CloseRequested` Eventを通知し、RuntimeHostが終了判断後に`destroy()`を呼ぶ
- `WM_DESTROY`で状態を`Destroyed`へ遷移し、`Destroyed` Eventを通知する。Factoryから所有権を返した公開済みMain Windowの破棄時だけ、終了通知として`PostQuitMessage`を呼ぶ
- Windows実装は`CreateWindowExW`中のCallbackで参照できる公開状態を保持し、Factoryから所有権を返す直前にだけ公開済みへ遷移する。生成途中のRollback破棄では`WM_DESTROY`を処理しても`PostQuitMessage`を呼ばない
- 同じClose Requestは、前回のRequestが処理されるまで重複Queueしない
- `destroy()`は同じThreadから複数回呼ばれた場合、既に`Destroyed`なら成功として扱う
- `Destroyed`を含む不正状態での`show()`とNative View取得はProgrammer Errorとし、`Result`へ変換せずAssertする

### Thread Affinity

- `WindowSystem`の作成ThreadをWindow Threadとする
- Window生成、表示、明示的なNative Window破棄、`Window`／`WindowSystem`のC++ Owner破棄、Message Pump、`try_pop_event()`、`state()`、`client_size()`、Native View取得はWindow Thread限定とする
- M02ではWindow状態とClient SizeのCross-thread参照を許可せず、同期Primitiveを公開契約へ追加しない
- Windows実装は作成時のThread IDを保持し、Thread違反をProgrammer ErrorとしてAssertで検出する
- `Window`または`WindowSystem`の所有権を別Threadへ移して解放することもThread違反とし、Debug／Developmentでは残存Native ResourceのCleanup前にAssertして終了する。ReleaseではこのOwner破棄自体を契約外とする
- Thread違反を`Result`のRecoverable Errorへ変換しない。ADR-0005に従い、Debug／DevelopmentではAssert後に終了し、ReleaseではThread前提違反の呼出自体を契約外とする
- Window ProcedureはWindow Thread上で同期実行され、Event Queueへ値を追加する
- M02では他ThreadからWindow ThreadへCommandを投稿するAPIを追加しない
- Callbackを公開せずOwner-local Event Queueを採用するため、Callback Lifetimeと再入規則はM02の公開契約へ持ち込まない

### Window Descriptor and Size

- `WindowDescriptor`はUTF-8 Titleと要求Client Sizeを持つ
- TitleはFactory呼出中だけ有効な非所有Viewとして受け取り、Windows実装が作成前にUTF-16へ変換する
- Client Sizeは符号なし32-bitの幅と高さで表現し、0を作成要求として許可しない
- Windows実装は要求Client SizeがWin32の符号付き`LONG`／`int`で表現できることをNarrowing前に検証する。表現不能な値は`CreateWindowExW`へ渡さず、Descriptor Errorを返す
- Windows実装は`AdjustWindowRectEx`系APIで要求Client Sizeから外枠込みWindow Sizeを計算し、非Client領域の加算をChecked Arithmeticで行う。計算結果が正の`int`範囲に収まらない場合もDescriptor Errorを返す
- EventのResize SizeはClient Areaの幅と高さを表す
- Minimize時の0 Sizeを通常Resizeとして通知しない
- `SIZE_RESTORED`を受信したとき、直前がMinimizedならRestore、それ以外は通常Resizeとして扱う
- Restore時は`GetClientRect`で最新の非0 Client Sizeを取得して通知する

### Window Events

M02のEvent種別は次に限定する。

```cpp
enum class WindowEventType
{
    CloseRequested,
    Resized,
    Minimized,
    Restored,
    Destroyed,
};

struct WindowSize
{
    std::uint32_t width;
    std::uint32_t height;
};

struct WindowEvent
{
    WindowEventType type;
    WindowSize clientSize;
};
```

- `Resized`と`Restored`は非0の`clientSize`を持つ
- `Minimized`、`CloseRequested`、`Destroyed`では`clientSize`を参照しない
- EventはWindowごとのFIFO Queueへ値として格納する
- `try_pop_event()`はQueueが空なら`false`を返し、出力値を変更しない
- Message Pump中に生成されたEventは、同じPump呼出の終了後から取得できる
- Event QueueのLifetimeはWindowと同一であり、Window破棄後にEventを保持しない
- M02ではQueue容量制限、Event Coalescing、Callback、Cross-thread配送を追加しない

### Message Pump

- `WindowSystem::pump_events()`は`PeekMessageW`と`PM_REMOVE`を使用するNon-blocking処理とする
- 呼出時点でThread QueueにあるMessageをQueueが空になるまで処理する
- `WM_QUIT`を検出しても、そのPump呼出で既に取得可能なMessageをDrainしてから`QuitRequested`を返す
- ActiveなFrame Loopでは毎Frame `pump_events()`を呼ぶ
- M02はBlocking Pumpを公開しない
- 将来、描画を停止するMinimized／Idle状態では、RuntimeHostの明示的な待機Policyとして`WaitMessage`またはFrame Pacingを追加できる
- Busy Waitは許可しない。M02 RuntimeHostは処理対象がないLoopで短時間Sleepを行う
- Message FilterでInputを除外せず、Window Threadの全MessageをDispatchする。Input Eventへの変換はScope外とする

### UTF-8 and UTF-16 Boundary

- Engine側の文字列契約はUTF-8とする
- Windows RuntimeHostは`wmain`でOSのUTF-16 Command Line Argumentを受け、`Cue.Platform.Windows`の変換APIを通してUTF-8へ変換する
- RuntimeHostはWindows SDK HeaderをIncludeせず、Unicode Win32 APIを直接呼ばない
- UTF-16 Command Line Argument変換APIは`std::wstring_view`を入力、`Result<std::string>`を出力とし、Win32型を公開しない
- Windows実装はUnicode版Win32 APIを明示的に呼び、Encoding-neutral Macroへ依存しない
- UTF-8からUTF-16への変換は`Cue.Platform.Windows`のPrivate Helperだけが担当する
- UTF-16からUTF-8への変換はWindows RuntimeHost入口向けの`Cue.Platform.Windows`公開Helperだけが担当する
- 無効なUTF-8、変換失敗、長さOverflowはWindow生成前に`Result` Errorとして返す
- 無効なUTF-16 Command Line Argument、変換失敗、長さOverflowも`Result` Errorとして返す
- 変換後のBufferはWindow生成呼出中だけ保持し、Platform公開型へ`std::wstring`を出さない
- Native Errorは`NativeError`へDomainと整数値で保持し、`DWORD`や`HRESULT`を公開しない

### Native Window Interop

通常のWindow契約とNative連携を分離する。

```cpp
enum class NativeWindowKind
{
    Win32,
};

class NativeWindowView final
{
public:
    [[nodiscard]] NativeWindowKind kind() const noexcept;
    [[nodiscard]] const void* value() const noexcept;
};

[[nodiscard]] Result<NativeWindowView> get_native_window_view(
    Window& a_window) noexcept;
```

- `NativeWindowView`は`Cue/Platform/Windows/WindowsWindowInterop.h`へ置き、`Cue.Platform`の通常HeaderからIncludeしない
- `value()`はWindows実装内部の`HWND`をOpaqueな非所有値として表す
- Viewは取得直後のInterop呼出中だけ有効で、保存、破棄、Close、Subclass化に使用しない
- Viewの有効期間は元Windowの`destroy()`開始までとする
- Viewを取得できるのはWindow Thread上の`Created`、`Visible`、`CloseRequested`状態だけとする
- `NativeWindowView`はWindows固有の診断、Test、Platform Adapterに限定し、`Cue.RHI`または`Cue.RHI.D3D12`へ渡さない
- RuntimeHostもViewまたは`value()`をRHI Backend Factoryへ転送しない
- `Cue.RHI`と`Cue.RHI.D3D12`は`WindowsWindowInterop.h`をIncludeせず、`Cue.Platform.Windows`へLinkしない
- PlatformとRHIの双方に依存できるAdapter TargetまたはPlatform非依存Surface契約は、M03以降のRHI Research Issueで決定する。Accepted ADRができるまでD3D12 Swap Chain連携を実装しない
- RHIとD3D12 BackendはWindow Ownerにならず、Native Handleを永続的なWindow同一性として扱わない
- `HWND`、`HINSTANCE`、`LRESULT`はPlatform非依存Header、RuntimeHost、RHI公開Headerへ出さない

このInterop形状はM02でWindows Testが実WindowへResize／Minimize／Restore操作を行うためにも使用できるが、Test専用APIではない。Windows固有Toolや診断Adapterも同じ短命Viewを利用できる。D3D12 Swap Chainとの契約には使用せず、最終Interop境界をM03以降のRHI Research Issueで新規に決定する。

### Failure and Cleanup

Window生成は次の順序で行い、失敗時は完了済み処理だけを逆順に戻す。

```text
Validate Descriptor
    -> Convert UTF-8 Title
    -> Calculate Window Rectangle
    -> Prepare Owner in Rollback State
    -> Register or acquire Window Class
    -> Create Native Window and Attach Owner
    -> Mark Owner as Published
    -> Return Window
```

- Descriptor検証、Win32 Size範囲検証、外枠込みSizeのOverflow検出、またはUTF変換失敗ではWindowを生成しない。Window Class登録前に判定できる失敗ではNative状態を変更しない
- Windows実装Factory、`WindowSystem`、`Window`は注入された`AssertContext`へ到達できる非所有参照を保持する。`noexcept`境界内のAllocation失敗では、その`FatalHandler`のEmergency Entry Pointを追加Allocationなしに呼ぶ
- Window Class登録失敗ではNative Error付きResultを返す
- Window生成失敗では、今回取得したWindow Class参照を解放する
- `CreateWindowExW`中にWindow ProcedureへOwnerを関連付け、失敗時にDangling Pointerを残さない
- Window生成成功後、Factoryから所有権を返す前の失敗ではRollback状態のまま`DestroyWindow`を呼び、`WM_DESTROY`から`WM_QUIT`を投稿しない。Handle無効化を確認してから所有権を破棄する
- 公開済みMain Windowの終了だけが`WM_QUIT`を投稿するため、Recoverableな生成失敗後も同じThreadでWindow生成を再試行できる
- 最後のWindow破棄後に限りWindow Classを解除する
- 解除失敗はDestructorから例外送出せず、利用可能なら診断Sinkへ報告する。M02ではGlobal Loggerを導入しない
- Recoverable Errorは下位層で重複Logせず、RuntimeHostの処理BoundaryでContextを追加して一度だけLogする

### Initial Scope

- Window Systemごとに単一Main Windowだけを生成できる
- 二つ目のWindow生成要求は明示的なErrorを返す
- Window Styleは通常のResizable Overlapped Windowに固定する
- Window PositionはOS既定に任せる
- DPI AwarenessのApplication Manifest／Process PolicyはM02で変更しない
- Fullscreen、Borderless、Multi-window、Parent／Child Window、PopupはScope外とする

### API Sketch

後続Issueでの命名と詳細はCoding Rulesおよび実装Reviewで調整できるが、責務は次の形を維持する。

```cpp
namespace cue
{
struct WindowDescriptor final
{
    std::string_view title;
    WindowSize clientSize;
};

enum class WindowState
{
    Created,
    Visible,
    CloseRequested,
    Destroyed,
};

enum class PumpStatus
{
    Running,
    QuitRequested,
};

class Window
{
public:
    virtual ~Window() = default;

    [[nodiscard]] virtual Result<void> show() noexcept = 0;
    [[nodiscard]] virtual Result<void> destroy() noexcept = 0;
    [[nodiscard]] virtual WindowState state() const noexcept = 0;
    [[nodiscard]] virtual WindowSize client_size() const noexcept = 0;
    [[nodiscard]] virtual bool try_pop_event(WindowEvent& a_event) noexcept = 0;
};

class WindowSystem
{
public:
    virtual ~WindowSystem() = default;

    [[nodiscard]] virtual Result<std::unique_ptr<Window>> create_window(
        const WindowDescriptor& a_descriptor) noexcept = 0;
    [[nodiscard]] virtual Result<PumpStatus> pump_events() noexcept = 0;
};
}
```

Windows実装Factoryは`Cue/Platform/Windows/WindowsPlatform.h`へ置く。

```cpp
namespace cue
{
[[nodiscard]] Result<std::unique_ptr<WindowSystem>>
create_windows_window_system(const AssertContext& a_assertContext) noexcept;
}
```

`a_assertContext`、参照先の`Logger`、`FatalHandler`のOwnerは、返された`WindowSystem`とそこから生成した全Windowより長く生存させる。`FatalHandler`はAllocation失敗時の`EmergencyHandler`も兼ねる。

### Lifecycle Sequences

通常終了:

```text
RuntimeHost        WindowSystem          WindowsWindow          Win32
    | create_window()   |                      |                  |
    |------------------>| CreateWindowExW      |----------------->|
    |<------------------| unique ownership     |                  |
    | show()            |--------------------->| ShowWindow       |
    | pump_events()     | PeekMessageW/DispatchMessageW          |
    |                   |<---------------------| WM_CLOSE         |
    | try_pop_event()   | CloseRequested       |                  |
    | destroy()         |--------------------->| DestroyWindow    |
    |                   |<---------------------| WM_DESTROY       |
    | try_pop_event()   | Destroyed            |                  |
    | destroy Window -> destroy WindowSystem -> unregister class |
```

生成失敗:

```text
Validate -> Convert -> Calculate Window Rectangle -> Register Class
                                                    -> CreateWindowExW fails
                                                    -> release class reference
                                                    -> unregister if last reference
                                                    -> Result Error with NativeError

CreateWindowExW succeeds -> later setup fails -> keep rollback state
                                             -> DestroyWindow / WM_DESTROY
                                             -> no PostQuitMessage
                                             -> release ownership and class reference
                                             -> retry remains possible
```

Resize、Minimize、Restore:

```text
WM_SIZE(nonzero) -> Resized(latest client size)
WM_SIZE(SIZE_MINIMIZED) -> Minimized(no resize request)
WM_SIZE(restored) -> GetClientRect -> Restored(nonzero latest client size)
```

## Consequences

### Positive

- RuntimeHostとPlatform契約からWin32型を排除できる
- Close RequestとResource破棄を分離し、上位層が終了判断できる
- Window、Window Class、Native Handleの所有権と破棄順序をTestできる
- Event QueueによりCallback再入とCross-thread LifetimeをM02から排除できる
- Windows固有Interopを短命なViewへ限定し、RHI連携の未決定境界を誤って固定しない

### Negative

- Windows実装FactoryとInterop用Headerが増える
- RuntimeHostはEventをPollし、Close Request後に明示的に`destroy()`を呼ぶ必要がある
- 単一Window限定のため、Editor Multi-windowではWindow識別子とSystem Lifetimeを再設計する可能性がある
- Opaqueな`const void*`は型安全性が限定され、Domain検査と利用範囲のReviewが必要になる

## Enforcement

- `Cue.Platform`の公開Header単体Compile TestでWindows SDK非依存を検査する
- CMake Target GraphでFoundation、Platform、Windows実装の依存方向を検査する
- Windows Lifecycle Testで要求Client Size、Win32表現上限と外枠加算Overflow、生成、表示、破棄、二重破棄、失敗Cleanupを検査する
- Event TestでClose、Resize、Minimize、Restore、Destroyedの順序と値を検査する
- RuntimeHost Smoke Testで初期化、Message Pump、Close、Shutdownを検査する
- Native Handleを通常Window HeaderまたはRHI公開Headerへ追加する変更は、このADRの変更としてReviewする

## Sources

- [Microsoft: Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)
- [Microsoft: WM_CLOSE message](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-close)
- [Microsoft: PeekMessageW function](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew)
- [Microsoft: WM_SIZE message](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size)
- [Unreal Engine: FGenericWindow](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/ApplicationCore/FGenericWindow)
- [Unity: FullScreenMode](https://docs.unity3d.com/ja/current/ScriptReference/FullScreenMode.html)
- [Godot Engine: DisplayServer](https://docs.godotengine.org/en/stable/classes/class_displayserver.html)

## Follow-up

- Issue #37で`Cue.Platform`と`Cue.Platform.Windows` Targetを追加する
- Issue #38でWin32 Windowの生成、表示、破棄を実装する
- Issue #39でMessage PumpとWindow Event変換を実装する
- Issue #40でRuntimeHost LifecycleとM02 Completion Gateを追加する
- M03以降のRHI Research IssueでPlatformとRHIの双方が利用できるSwap Chain Interop境界を決定する
