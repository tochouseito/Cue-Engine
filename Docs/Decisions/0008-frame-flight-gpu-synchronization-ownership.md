# ADR-0008: Frames in Flight and GPU Synchronization Ownership

- Status: Accepted
- Date: 2026-08-22
- Decision Owners: CueEngine Project

## Context

M04では、Render Target Clearへ進む前に、Direct Graphics Queue、Fence、Command
Allocator、Graphics Command List、Swap Chain Back Bufferを安全に再利用できる最小Frame基盤を
確立する必要がある。

Direct3D 12では、Command AllocatorをGPU実行中にResetすると未定義動作になり、Swap Chainの
Back Buffer参照を残したまま`ResizeBuffers`を呼ぶと失敗する。CPUが毎Frame無条件にGPU完了を
待てば安全性は得られるが、CPUとGPUの並行実行を失い、Flip ModelでCPUが無制限に先行すれば
LatencyとMemory使用量を制御できない。

ADR-0007は、D3D12 BackendがGraphics Queueを所有し、Presentation ContextがSwap Chain、Frame
Context、Back Buffer、RTVを所有する境界を決定した。このADRは、その境界を変更せず、M04で必要な
Frames in Flight数、Fence値、Command AllocatorとCommand Listの再利用条件、Resize、Shutdown、
Device Removal時の状態遷移を決定する。

Texture、Shader、Pipeline、Draw Command、Resource Barrier、Present Loop、Multi-Queue、Parallel
Recording、FrameGraphは決定しない。

## Legacy Reference

### Legacy Problem

旧CueEngineも、複数FrameのCommand AllocatorとCommand ContextをFence完了後に再利用し、
Swap Chain ResizeとShutdownでGPU完了を待つ必要があった。

### Legacy Approach

旧実装はGraphics、Compute、Copy QueueとCommand Context PoolをClear前から構築し、Fence付きLeaseで
Command Contextを再利用した。

### Legacy Strengths

- GPU完了前のCommand Allocator再利用を避ける同期構造を持っていた
- 複数Queueと複数Command Contextへ拡張できる形を持っていた
- Frame Resourceの再利用にFenceを利用していた

### Legacy Problems

- Render Target Clearに不要なCompute、Copy Queueと汎用Poolまで初期化前提になった
- Frame Slot、Swap Chain Buffer、Command Contextの対応をAPIから判断しにくかった
- Fence Wait失敗、Device Removal、Shutdown失敗時に安全に解放できる範囲が明確でなかった
- 汎用LeaseとManagerが所有権とThread Affinityを間接化した

### Current Requirements

- 初期Scopeを一つのDirect Graphics Queueへ限定する
- Command Allocatorを、そのAllocatorで記録した全WorkのFence完了後だけResetする
- CPUは毎Frame無条件にGPU完了を待たず、再利用対象Frame Slotが未完了の場合だけ待つ
- Swap Chain BufferとFrame Slotの対応を一意にする
- Fence WaitをBusy Pollingまたは無期限待機にしない
- ResizeとShutdownでGPU完了、Device Removal、安全に解放できない状態を区別する
- Native Queue、Fence、Event、Allocator、Command Listを公開RHI APIへ出さない
- M04の単一ThreadモデルとADR-0005、ADR-0007のError契約を維持する

### New Design

初期構成は2 BufferのFlip Model Swap Chainと2個のFrame Contextを一対一対応させる。D3D12
Backendは一つのDirect Graphics Queue、一つのFence、一つの待機Eventを所有する。各Presentation
Contextは一つのGraphics Command Listを所有し、各Frame Contextは一つのDirect Command Allocatorと
最後にそのAllocatorを使用したSubmitのFence値を所有する。

Frame開始時は`GetCurrentBackBufferIndex`から対象Frame Contextを決定する。対象の再利用Fence値が
未完了の場合だけ、共有Fence Eventで有限時間待つ。完了確認後にAllocator、Command Listの順で
Resetする。Close、Execute、Signal後に、そのSignal値を対象Frame Contextへ保存する。

## Reference Engine Comparison

| Engine | Relevant Approach | Strengths | Trade-offs for CueEngine M04 |
| --- | --- | --- | --- |
| Unreal Engine | RHI Command List、GPU Fence、RHI Thread、複数Pipelineを抽象化する | Platform差と並列Submitを大規模Rendererから隠蔽できる | M04で同等のThread、Pipeline、Command List Executorを導入すると、単一QueueのClearに対して所有権と同期が過剰になる |
| Unity | `CommandBuffer`と`GraphicsFence`でQueue間またはCPU/GPU同期を上位APIへ公開し、Resource依存の一部をEngineが処理する | Script利用者がNative Fence値やAllocator寿命を管理せずに済む | CueEngineのBackend内部実装段階では、Allocator再利用条件とNative失敗を隠す前に明示してTestする必要がある |
| Godot | `RenderingDevice`がCommand List相当の操作と`submit()`／`sync()`を抽象化する | 現代的Graphics APIを共通APIから扱える | `sync()`直後のCPU待機は単純だが、毎Frame適用するとCPU/GPU並行性を失う。M04ではFrame Slot Fenceに限定して待つ |
| CueEngine M04 | 一つのDirect Queue、Queue-global Fence、Presentation ContextごとのCommand List、Frame SlotごとのAllocatorとFence値をPrivate実装する | D3D12の再利用条件を直接検証でき、将来APIを先回りせずにClearまで進められる | Multi-Queue、Parallel Recording、動的Frame Count、汎用Command Poolを後続Issueへ延期する |

比較の結果、M04では大規模Engineの抽象化機能量を採用せず、D3D12の必須同期条件を最小所有型へ
固定する。上位利用者へNative Fenceを公開しない点は各Reference Engineと共通するが、内部状態と
失敗時のResource保持規則はCueEngine固有の診断可能な契約として明示する。

## Decision

### Scope and Constants

- Graphics Queueは`D3D12_COMMAND_LIST_TYPE_DIRECT`を一つだけ生成する
- Queue Descriptorは`D3D12_COMMAND_QUEUE_PRIORITY_NORMAL`、`D3D12_COMMAND_QUEUE_FLAG_NONE`、
  `NodeMask = 0`とする
- Queue、Fence、Eventは最初のPresentation生成時ではなくD3D12 Backend生成時に作成する
- Swap Chain Buffer Countは2とする
- Frames in Flightは2とし、Swap Chain Buffer Countと常に等しくする
- Frame Context数をRuntime設定または公開Capabilityにしない
- Frame ContextはSwap Chain Back Buffer Indexと一対一に対応する
- Compute Queue、Copy Queue、Bundle、Command Pool、Allocator Poolを追加しない
- 初期Graphics ThreadはADR-0007どおりRuntimeHost Main ThreadかつWindow Threadとする

2 BufferはFlip Modelの最小要件を満たし、M04で余分なBack BufferとAllocatorを持たずにCPUとGPUを
最大1 Frame分先行させるための初期値である。3 Buffer以上のThroughputまたはPacing上の効果は、
Present Loopと測定条件が揃った後に別Issueで評価する。2 Bufferが最高性能であるとは主張しない。

### Ownership and Lifetime

| Object or State | Owner | Lifetime and Access Rule |
| --- | --- | --- |
| Direct Graphics Queue | D3D12 Backend | Fence、全Presentation Context、Frame Context、Swap Chainより長く生存する |
| Queue Fence | D3D12 Backend | Queueと同時に生成し、全Signal、Completed Value、CPU Waitを一元化する |
| Fence Wait Event | D3D12 Backend | 一度に一つだけ所有し、Fenceより後に生成してFenceより先に閉じる。生成Threadからだけ使用し、異常Wait後の非terminal継続時は未signaled Eventへ置換する |
| Next Fence Value | D3D12 Backend | 次にSignalする値を保持し、Context間で重複させない |
| Last Signaled Fence | D3D12 Backend | Signal成功、またはSignal失敗後に予約値完了を証明した最大値を保持する |
| Presentation Context | Composition Root | BackendとWindowより先に明示`shutdown()`して破棄する |
| Graphics Command List | Presentation Context | 一つだけ所有し、Frame ContextのAllocatorを切り替えて直列に再利用する |
| Frame Context | Presentation Context | 2個を所有し、Back Buffer Indexで選択する |
| Direct Command Allocator | 対応Frame Context | 対応`reuseFenceValue`の完了後だけResetする |
| Reuse Fence Value | 対応Frame Context | 0は未Submit、1以上は最後にAllocatorを使用したSubmitを表す |
| Back Buffer Resource、RTV Slot、Resource State | 対応Frame Context | Allocatorと同じBack Buffer IndexのFrame Contextへまとめて保持する |
| RTV Descriptor Heap | Presentation Context | 2 Slotだけを所有し、全Frame ContextとBack Bufferより長く生存する |
| Swap ChainとCurrent Index | Presentation Context | `GetCurrentBackBufferIndex`を正本とし、CPUのModulo計算で進めない |

BackendはPresentation Contextを所有しない。ADR-0007どおり有効Context数だけを追跡し、Contextが
残るBackend Shutdownを拒否する。Queue、Fence、Eventは複数Presentation Contextから共有できるが、
初期Thread規則により同時呼出は発生しない。

- Presentation Contextを完全に構築した後だけBackendの有効Context数を増やす
- 構築途中の失敗は登録せず、生成済みObjectを逆順に解放する
- 正常またはDevice Removed Shutdownで安全な解放が完了した時だけ一度登録解除する
- `Unavailable`では登録を解除せず、Backend Shutdownのactive count GateとResource到達可能性を維持する
- 二重登録解除とCount underflowはProgrammer ErrorとしてAssertする

### Fence Value Rules

- Fence生成時のCompleted Valueは0とする
- 最初にSignalする値は1とする
- Engineが発行するFence値は成功、失敗にかかわらず巻き戻さず、再利用しない
- Signal前にFence値を予約して`nextFenceValue`を進め、Signal失敗時も予約値へ戻さない
- `ID3D12Fence`自体が値の巻き戻しを許しても、CueEngineは厳密な単調増加だけを許可する
- `UINT64_MAX`は`GetCompletedValue`がDevice Removalを示す値として予約し、Signalしない
- Signal可能な最大値は`UINT64_MAX - 1`とする
- 次の値が予約域へ入る場合はGPU WorkをSubmitする前に`RHI.FenceValueExhausted`を返し、
  Backendと全Contextの新しいWork受付を停止する
- Fence枯渇だけでは`Unavailable`へ遷移しない。既存`lastSignaledFence`の完了を有限時間で確認できれば
  terminal Signalなしで安全な通常Shutdownを行う
- `submit_frame`が`Closed`でFence枯渇を検出した場合はExecuteせず、同じ完了済みAllocatorでCommand
  ListをResetして空のままCloseし、`IdleClosed`へ遷移する。ResetまたはCloseが失敗してもGPUへ未投入の
  内容なので、既存`lastSignaledFence`をDrainした後に安全なContext Cleanupを行う
- Fence枯渇後の待機で完了もDevice Removalも証明できない場合だけ`Unavailable`へ遷移する
- Queueの`Signal`成功時、または失敗後の再確認で予約値の完了を証明できた時に、Backendの
  `lastSignaledFence`をそのSignal値へ更新する
- Frame Submitの通常SignalまたはPresent Error後の補完Signalについて成功または予約値の完了を証明できた
  場合だけ、Allocatorを使用した対象Frame Contextの`reuseFenceValue`を同じSignal値へ更新する。Context
  terminal SignalとBackend
  terminal Signalには対象Frame Contextがないため、`reuseFenceValue`を更新しない
- Presentation Contextに属する通常Frame、補完Signal、Context terminal Signalについて成功または予約値の
  完了を証明できた場合は、そのPresentation Contextの`lastSubmittedFence`を同じSignal値へ更新する。
  Backend terminal Signalには対象Presentation Contextがないため、`lastSubmittedFence`を更新しない

#### Signal Failure Matrix

`Signal`が失敗した場合は、後述の有限Wait Policyで予約値の完了を確認し、Device Removalも再確認する。
予約値を巻き戻さず、完了を証明できない予約値はどの追跡値にも保存しない。Signal種別ごとの処理は次の
とおりとする。

| Signal Origin | 予約値の完了を証明 | Device Removalを確認 | 完了もRemovalも証明不能 |
| --- | --- | --- | --- |
| 通常Frame Signal | Backend、対象Frame、Presentationの3値を更新し、受付停止付き`Submitted`へ遷移する。Signal ErrorをPrimary、完了確認中のWait ErrorをSecondary Contextとして通常Shutdownを許可する | 3値を更新せずBackendとContextを`DeviceRemoved`へ遷移する。`RHI.DeviceRemoved`をPrimary、Removal ReasonをNative Error、Signal ErrorとWait ErrorをCause ContextとしてDREDとDevice Removed Cleanupへ進む | 3値を更新せずBackendとContextを`Unavailable`へ遷移する。Signal ErrorをPrimary、Wait／Removal確認ErrorをSecondary Contextとして全Resourceと登録を保持する |
| Present Error後の補完Frame Signal | 3値を更新し、受付停止付き`Submitted`へ遷移する。Present ErrorをPrimary、Signal Error、完了確認中のWait Errorを発生順のSecondary Contextとして通常Shutdownを許可する | 3値を更新せずBackendとContextを`DeviceRemoved`へ遷移する。`RHI.DeviceRemoved`をPrimary、Removal ReasonをNative Error、Present Error、Signal Error、Wait ErrorをCause ContextとしてDREDとDevice Removed Cleanupへ進む | 3値を更新せずBackendとContextを`Unavailable`へ遷移する。Present ErrorをPrimary、Signal／Wait／Removal確認ErrorをSecondary Contextとして全Resourceと登録を保持する |
| Context terminal Signal | Backendの`lastSignaledFence`と対象Presentationの`lastSubmittedFence`だけを更新する。完了確認中にWait ErrorがあればEvent置換成功後だけContext Resourceを逆順解放して`Shutdown`へ遷移し、Signal ErrorをPrimary、Wait ErrorをSecondary Contextとして返す。Event置換失敗時は`Unavailable`としてResourceと登録を保持する | 追跡値を更新せずBackendと対象Contextを`DeviceRemoved`へ遷移する。`RHI.DeviceRemoved`をPrimary、Removal ReasonをNative Error、Signal ErrorとWait ErrorをCause ContextとしてDREDとContext Cleanupへ進む | 追跡値を更新せずBackendと対象Contextを`Unavailable`へ遷移する。Signal ErrorをPrimary、Wait／Removal確認ErrorをSecondary ContextとしてContext ResourceとBackend登録を保持する |
| Backend terminal Signal | Backendの`lastSignaledFence`だけを更新し、Event、Fence、Queue、Deviceを逆順解放して`Shutdown`へ遷移する。Signal ErrorをPrimary、完了確認中のWait ErrorをSecondary Contextとして返す | 追跡値を更新せずBackendだけを`DeviceRemoved`へ遷移する。`RHI.DeviceRemoved`をPrimary、Removal ReasonをNative Error、Signal ErrorとWait ErrorをCause ContextとしてDREDとBackend Cleanupへ進む | 追跡値を更新せずBackendだけを`Unavailable`へ遷移する。Signal ErrorをPrimary、Wait／Removal確認ErrorをSecondary ContextとしてEvent、Fence、Queue、Deviceを保持する |

Context terminal Signalには対象Frameと先行Present Errorがなく、Backend terminal Signalには対象Contextも
先行Present Errorもない。Composition Rootが以前の操作ErrorとShutdown Errorを統合する場合は、この表の
Resultを上書きせず、上位Contextとして両方を保持する。
完了確認中にWait Errorが発生していない場合は、表中のWait Errorを省略する。

Fence値のOverflowは実運用上到達困難でも、符号なしWrapによる古いFrameの誤完了判定を許可しない。

### Fence Wait Policy

通常のCPU Waitは次の順序で行う。

1. 対象Fence値が0なら成功する
2. `GetCompletedValue`を一度取得する
3. 戻り値が`UINT64_MAX`ならDevice Removal経路へ遷移する
4. Completed Valueが対象以上ならEventを登録せず成功する
5. `SetEventOnCompletion`でBackend所有のAuto-reset Eventを登録する
6. Event登録成功時だけ`WaitForSingleObject`で有限時間待つ
7. `WAIT_FAILED`なら直後に`GetLastError`を取得し、他のWin32またはCOM APIを呼ぶ前にNative Errorへ保存する
8. `WAIT_OBJECT_0`なら`GetCompletedValue`を再確認し、対象以上だけを成功とする
9. Event登録失敗、Timeout、保存済み`WAIT_FAILED`、予期しないWait結果、Event後の未完了では、そのNative
   ErrorまたはWait ErrorをPrimary候補として保存する
10. `GetDeviceRemovedReason`を一度確認し、Device Removalなら`RHI.DeviceRemoved`をPrimary、保存済みWait
    ErrorをCause ContextとしてDevice Removed経路へ遷移する
11. Removalでない場合は、`Unavailable`確定前に`GetCompletedValue`を最後にもう一度取得する
12. 最終値が`UINT64_MAX`ならDevice Removal経路へ遷移する
13. 最終値が対象以上ならGPU完了済みとして安全なCleanupまたは再利用を許可するが、保存済みWait Errorは
    上位へ返す
14. 最終値も対象未満で、完了もRemovalも証明できない場合だけ`Unavailable`へ遷移する

Signal成功後のWait単独失敗では、成功時に保存済みのFence追跡値を消去または巻き戻さない。最終確認で
完了を証明できればWait Errorを返しながら安全なCleanupを続け、Device Removalなら`RHI.DeviceRemoved`を
PrimaryとしてDREDとCleanupへ進む。どちらも証明できない場合はWait ErrorをPrimaryとして`Unavailable`へ
遷移し、保存済み追跡値、Resource、登録を保持する。この経路にSignal Errorは追加しない。

#### Fence Wait Event Recovery

Auto-reset Eventを共有するため、Timeout、`WAIT_FAILED`、予期しないWait結果、Event後の未完了から最終
Completed Value確認で完了へ変わるraceでは、旧Eventがsignaledのまま残る可能性を考慮する。

- Backend terminal Waitだけは直後にEventを閉じてBackend Cleanupへ進むため、Eventを再作成しない
- Context terminal Wait後もBackend所有Eventは他Contextまたは後続Backend terminal Waitで再利用するため、
  異常Wait後の最終確認で完了を証明した場合はContext Resource解放と登録解除より前にEventを置換する
- Backend terminal以外のWaitは、対象Fence完了を証明した後、`Ready`、Frame受付維持、またはContext Cleanupを
  決定する前に旧Eventを
  `CloseHandle`で閉じ、HandleをNullへ更新してから同じAuto-reset／初期未signaled条件で新Eventを作成する
- 旧Eventは対象Fence完了の証明前に閉じない
- Event置換成功後だけBackendとContextの`Ready`、Frame受付、次回Fence Waitを維持する。元のWait Errorは
  上位へ返す
- 旧EventのCloseまたは新Event生成が失敗した場合は新しいWorkを停止し、Backendと対象Contextを
  `Unavailable`へ遷移する。GPU参照可能Resource、登録、まだOpenなEventを保持し、既に正常Closeした旧Eventを
  再利用しない
- 新Eventへの置換後、次の未完了Fence Waitは必ず新Eventへ登録し、旧Eventのstale signalを観測しない

- Busy Pollingを使用しない
- `SetEventOnCompletion`へNull Handleを渡す同期Waitを使用しない
- RuntimeHostは`D3d12BackendDescriptor::gpuWaitTimeoutMilliseconds`へ有限時間を明示する
- ProductionのRuntimeHost既定Wait Timeoutは5,000 msとする
- Backendは0、`INFINITE`相当値、60,000 msを超える値をInvalid Configuration Errorにする
- Testは同じDescriptorへ短いTimeoutを指定できるが、Production公開APIへWindows Handleを出さない
- TimeoutはGPU Resourceを安全に破棄できる証明ではないため、通常ShutdownへFallbackしない
- `Unavailable`ではEvent、Fence、Queue、GPU参照可能Resourceを解放しない

5,000 msは性能目標ではなく、無期限停止を避けてTDR前後の異常を上位Fatal経路へ渡すための初期運用値で
ある。Hardware、Driver、Workloadに基づく変更は、Hang診断と実測条件を持つ別Issueで行う。

### Command Allocator and Command List States

各Presentation ContextはCommand List状態をD3D12 Objectとは別に追跡する。

| State | Meaning | Allowed Next Operation |
| --- | --- | --- |
| `Initial` | Native Command List生成直後でRecording中 | 初期化用`Close`だけを許可する |
| `IdleClosed` | 初期化または明示Discard後の空Command ListがClose済み | 完了済みAllocatorを指定した`Reset`を許可する |
| `Submitted` | QueueへExecute済みで、対応FenceをSignal済み | 完了済みAllocatorを指定した`Reset`だけを許可する |
| `Recording` | AllocatorとCommand ListをReset済みで記録中 | Command記録と`Close`だけを許可する |
| `RecordingCloseFailed` | Native `Close`が失敗し、未Submit内容を実行できない | Context Shutdownだけを許可する |
| `Closed` | Frame Commandの記録終了済みで、まだExecuteしていない | `Execute`または未Submit内容の明示Discardだけを許可する |
| `ExecutedAwaitingPresent` | Execute済みで、Presentをまだ試行していない | Presentを一度だけ許可する |
| `ExecutedUnfenced` | Present試行済みだが、ExecuteしたWorkを覆うFence Signalが未成功 | Signal成功、予約値の完了証明、Device Removed、`Unavailable`への遷移だけを許可する |

生成には既存の`ID3D12Device::CreateCommandList`を使用し、生成直後の`Initial`状態を一度`Close`して
`IdleClosed`へ遷移する。`ID3D12Device4::CreateCommandList1`を使うためだけにDevice Interface要件を
拡張しない。

Frame記録の規則:

- `begin_frame`は対象Frame ContextのFence完了を確認してからAllocatorをResetする
- `begin_frame`のEvent登録またはWaitが失敗しても最終Completed Valueで対象Fenceの完了を証明できた場合は、
  その呼出ではAllocatorとCommand ListをResetせず`Submitted`を維持し、Fence Wait Event置換成功後にWait
  Errorを返す。Contextは置換成功時だけ`Ready`とFrame受付を維持し、次の`begin_frame`が初回Completed Value
  確認後に安全にResetできる。置換失敗時はResetせず`Unavailable`へ遷移する
- `begin_frame`のWait失敗後にDevice Removalを確認した場合はAllocatorをResetせずBackendとContextを
  `DeviceRemoved`へ遷移してDRED経路へ進む。完了もRemovalも証明できない場合はAllocator、Back Buffer、
  Context Resource、Backend登録を保持してBackendとContextを`Unavailable`へ遷移する
- Allocator Reset成功後にCommand ListをそのAllocatorでResetし、`Recording`へ遷移する
- Command List Reset失敗時はAllocatorを再利用可能だがFrame開始失敗としてErrorを返す
- `Recording`以外でCommandを記録しない
- `close_frame`は`Recording`からだけ呼び、成功後`Closed`へ遷移する
- `close_frame`のNative `Close`が失敗した場合はDevice Removalを確認する。Removalでなければ新しいFrame受付を
  停止して`RecordingCloseFailed`へ遷移し、未Submit内容をExecute、Reset、再CloseせずNative Error付き
  `Result`を返す
- `submit_frame`は`Closed`からだけ呼び、Execute直後に`ExecutedAwaitingPresent`へ遷移する
- Presentを一度試行した直後に、結果を処理する前に`ExecutedUnfenced`へ遷移する
- `ExecutedAwaitingPresent`と`ExecutedUnfenced`ではCommand ListとAllocatorをReset、再Execute、破棄しない
- Signal成功後、またはSignal失敗後に予約値の完了を証明できた場合は`Submitted`へ遷移し、Frame Contextの
  `reuseFenceValue`、Presentation Contextの`lastSubmittedFence`、Backendの`lastSignaledFence`へ同じ
  Fence値を保存する。失敗後の完了証明では新規Frame受付を停止し、元のNative Errorを返す
- State順序違反とFrame Index範囲外はProgrammer ErrorとしてDebug／DevelopmentでAssertする
- Native API失敗はNative Error付き`Result`で返す
- Command ListはExecute後に別の完了済みAllocatorを使ってResetできるが、Allocator自身は対応Fence完了前に
  Resetしない

`ExecuteCommandLists`は戻り値を持たない。事前State検査、Debug Layer、直後のQueue Signal、
Device Removal確認を組み合わせて失敗を診断する。

### Frame and Back Buffer Index

- 初回Frameと各Present後に`IDXGISwapChain3::GetCurrentBackBufferIndex`を取得する
- Current Back Buffer IndexをPresentation Contextが保持し、次の`begin_frame`で使用する
- Indexが2以上なら新しいFrame受付を停止して`RHI.InvalidBackBufferIndex` Errorを返す。Index異常だけでは
  GPU未完了を意味しないため`Unavailable`へ遷移せず、既存Fenceを待つ通常Shutdownを許可する
- Frame ContextとBack Buffer Slotは同じIndexで参照する
- CPU側で`(index + 1) % 2`を正本として使用しない
- Back Buffer Resource Stateは対応Slotが所有し、Swap Chain生成直後とPresent後は`Present`とする
- M04ではResource Barrierを記録せず、M05で`Present -> RenderTarget -> Present`遷移を追加する

### Normal Frame Sequence

```text
Presentation Context       Frame Context       Command List       Queue / Fence       Swap Chain
        | current index ----------->|                 |                  |                   |
        | check reuse fence --------|-------------------------------> completed?            |
        | wait only if needed ------|-------------------------------> event                 |
        | allocator reset --------->|                 |                  |                   |
        | command list reset -------|---------------->| Recording        |                   |
        | record commands ----------|---------------->|                  |                   |
        | close ---------------------|---------------->| Closed           |                   |
        | execute -------------------------- ExecutedAwaitingPresent -------------------------->|
        | present ------------------------------ ExecutedUnfenced ------------------------->|
        | signal next fence ------------------------------------>|                              |
        | store frame reuse + presentation lastSubmitted fence -------->| Submitted         |
        | get current index <----------------------------------------------------------------|
```

CPUは次に選択されたFrame Contextの`reuseFenceValue`が未完了の場合だけ停止する。Presentの直前または
直後に毎回GPU Idleを待たない。

Present成功後にQueue FenceをSignalする。`DXGI_STATUS_OCCLUDED`はDevice Failureにせず、同じSignalと
Frame再利用Fence更新を行って上位へOccluded状態として伝える。PresentがDevice RemovedまたはResetを
返した場合は新しいSignalを発行せず、Backendを先に`DeviceRemoved`へ遷移してDRED経路へ入る。それ以外の
Present失敗では、既にExecuteしたWorkを覆うFence Signalを試みる。Signal成功時はFrame Contextへ値を
保存し、新しいFrame受付を停止する。Contextは安全なFence Waitと通常Shutdownを行うため`Ready`を維持し、
次の`begin_frame`は`RHI.PresentationStopped`を返す。Composition Rootは元のPresent Errorを処理して通常
Shutdownを選ぶ。Signalも失敗した場合は予約値の完了またはDevice Removalを確認する。完了を証明できた
場合は3種類のFence値を保存して`Submitted`へ遷移し、Present ErrorをPrimary、Signal ErrorをSecondary
Contextとして返した後に通常Shutdownを許可する。Device Removalを確認した場合は3種類の値を更新せず、
BackendとContextを`DeviceRemoved`へ遷移する。`RHI.DeviceRemoved`をPrimary、Removal ReasonをNative Error、
Present ErrorとSignal ErrorをCause Contextとして保持し、DREDをBest-effortで収集してFenceを待たないDevice
Removed Cleanupへ進む。どちらも証明できなければ`Unavailable`へ遷移し、Present ErrorをPrimary、Signal、
Wait、Removal確認のErrorを発生順のSecondary Contextとして保持する。

### Resize Sequence

ResizeはFrame境界でだけ開始し、Command Listが`IdleClosed`または`Submitted`の場合だけ呼ぶ。

```text
PresentationContext.resize(newSize)
    -> same nonzero size and not suspended: preserve frame acceptance and return success
    -> zero width or height: remember pending size, stop frame acceptance, and return success
    -> nonzero restore at the existing size: clear suspension, resume frame acceptance, and return success
    -> for a size change, stop frame acceptance
    -> wait for Presentation Context last submitted fence with finite timeout
    -> reset completed allocator and command list, then close an empty list as IdleClosed
    -> release every Back Buffer Resource and RTV Slot reference
    -> call ResizeBuffers while preserving Buffer Count = 2
    -> acquire two Back Buffers and recreate matching RTVs
    -> set both Back Buffer states to Present
    -> clear Frame Context reuseFenceValue to 0 after proven GPU idle
    -> query and validate GetCurrentBackBufferIndex
    -> accept new frames
```

Resizeの失敗契約を処理段階で分ける。

- 引数検証、同一Size判定、0 Size延期までの失敗はGPU操作前なので、既存ResourceとFrame受付状態を変更せず
  Errorを返す
- Fence Waitで完了もDevice Removalも証明できない場合は`Unavailable`へ遷移し、全Resourceと登録を保持する
- Fence完了後はGPU Idleが証明済みである。Allocator Reset、Command List Reset／Close、Back Buffer解放、
  `ResizeBuffers`、Back Buffer再取得、RTV再構築のいずれが失敗しても、通常Frame受付を再開しない
- GPU Idle確認後の失敗では、Command List状態にかかわらず、Command List、Back Buffer、Allocator、RTV Heap、
  Swap ChainをBest-effortで安全に解放し、Backend登録を解除してContextを`Shutdown`へ遷移する
- GPU Idle確認後の失敗はPrimary Errorを返し、Cleanup ErrorをSecondary Contextとして保持する。
  安全な解放が可能なため`Unavailable`へ遷移しない
- Native API失敗がDevice Removalを示す場合はBackendを先に`DeviceRemoved`へ遷移し、DRED後にDevice Removed
  Cleanupを行う

したがって、ResetまたはClose後に「旧状態へ戻す」Rollbackは行わない。Resizeは完全成功してFrame受付を
再開するか、安全にContextをShutdownするかのどちらかとする。

### Shutdown Sequence

Presentation Contextの通常Shutdown:

```text
stop accepting frames
    -> require Command List state is IdleClosed, Submitted, or RecordingCloseFailed
    -> reserve and signal a Context terminal fence on the shared Queue
    -> after Signal success or reservation completion proof, store the terminal value
    -> wait for the terminal fence with finite timeout
    -> release Graphics Command List
    -> release Back Buffer Resources and clear RTV Slot metadata
    -> release Frame Context allocators
    -> release RTV Descriptor Heap
    -> release Swap Chain
    -> unregister from Backend
    -> state = Shutdown
```

Context terminal fenceは、そのContextが最後にSubmitしたWorkより後ろへ同じThreadからSignalする。
Workを一度もSubmitしていないContextも同じ経路を使い、Shutdown専用の分岐を増やさない。Signal失敗は
Signal Failure MatrixのContext terminal行に従う。Signal成功後のWait単独失敗はFence Wait Policyに従い、
保存済みのBackendとPresentationのterminal値を維持する。最終確認で完了を証明できればWait Errorを返しつつ
Event置換成功後に安全に解放し、Device RemovalならDREDとDevice Removed Cleanupを行う。Event置換失敗、
または完了もRemovalも証明できない場合だけResourceとBackend登録を保持し、ContextとBackendを`Unavailable`へ
遷移する。
Fence枯渇済みの場合だけterminal Signalを省略し、既存`lastSignaledFence`の完了確認後に同じ解放順序へ
進む。`RecordingCloseFailed`でも失敗したCommand Listを再CloseまたはExecuteせず、terminal SignalとWaitで
既存Queue Workの完了を証明した後にCommand Listを含むContext Resourceを解放する。

D3D12 Backendの通常Shutdown:

```text
verify active Presentation Context count == 0
    -> stop accepting Backend work
    -> normally reserve and signal a terminal fence after all queued work
    -> when fence values are exhausted, use the existing lastSignaledFence without a new signal
    -> wait for the selected terminal value with finite timeout
    -> close Fence Event
    -> release Fence
    -> release Direct Graphics Queue
    -> run Device diagnostics
    -> release Device, Adapter, Factory
    -> state = Shutdown
```

Backend Shutdownのterminal fenceは、Presentation Context外でBackendが投入したWorkも含め、Queue上の
全Workを覆う。Workを一度も投入していない場合でも、Queue生成後は同じSignal／Wait経路を使用して
Shutdown実装を一つに保つ。Signal失敗はSignal Failure MatrixのBackend terminal行に従う。Signal成功後の
Wait単独失敗は保存済みのBackend terminal値を維持してFence Wait Policyに従い、いずれもBackendだけを
遷移対象とする。

### Device Removal and Unavailable

FenceまたはQueue操作で失敗した場合は、ADR-0007の状態規則を適用する。

| Observation | State | Cleanup Rule |
| --- | --- | --- |
| Fence完了を確認できた | `Ready`または`Shutdown`へ進行 | 完了Fenceが覆うResourceを解放できる |
| Signal失敗後に予約値の完了を確認できた | Frameは受付停止付き`Submitted`、terminal経路は`Shutdown`へ進行 | 対象追跡値を更新し、元のErrorを返しながら完了Fenceが覆うResourceを安全に解放できる |
| `GetDeviceRemovedReason`が失敗HRESULT | `DeviceRemoved` | Fenceを待たずDREDをBest-effort収集し、Native Objectを逆順に解放する |
| Wait Timeout、`WAIT_FAILED`、Signal失敗後に完了もRemovalも証明不能 | `Unavailable` | GPUが参照し得るAllocator、Back Buffer、RTV、Swap Chain、Queue、Fence、Event、Deviceを保持する |
| Fence Completed Valueが`UINT64_MAX` | `DeviceRemoved` | Removal ReasonとDREDを収集し、Device Removed経路で解放する |

`Unavailable`は通常復帰、Retry、Destructor Cleanupを許可しない。RuntimeHostはErrorを一度だけFatal
Dispatcherへ渡し、Stack UnwindせずProcessを終了する。

同期基盤のErrorは`Cue.RHI.D3D12` Domainで少なくともQueue生成、Fence生成、Event生成、Signal、
Wait登録、Wait Timeout、Wait Primitive、Fence Overflow、Command Allocator Reset、Command List Reset、
Command List Close、Present、Invalid Back Buffer Indexを区別する。HRESULTとWin32 ErrorはADR-0005に
従ってNative Errorへ保持し、Shutdown中の複数失敗はM03で確立したPrimary Error維持とSecondary Error
Context統合を再利用する。

### Single Graphics Queue Rationale

Render Target Clearまでに必要なGPU WorkはDirect Queue一つで表現できる。Compute／Copy Queueを追加すると、
QueueごとのFence、Cross-Queue Wait、Resource Ownership Transfer、Shutdown順序が必要になり、M04の
Acceptance Gateを超える。

Single Queueは将来のMulti-Queueを禁止する決定ではない。Async ComputeまたはCopy Uploadの実測要件が
現れた時点で、QueueごとのFence DomainとResource State Ownershipを別ADRで決定する。

### API Sketch

公開RHI契約へNative同期型を追加しない。M04のD3D12 Private実装は概念的に次の所有構造を持つ。

```cpp
class D3d12QueueState final
{
  public:
    [[nodiscard]] cue::Result<std::uint64_t> signal() noexcept;
    [[nodiscard]] cue::Result<bool> is_complete(std::uint64_t a_value) noexcept;
    [[nodiscard]] cue::Result<void> wait(std::uint64_t a_value) noexcept;

  private:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    void *m_event;
    std::uint64_t m_nextFenceValue;
    std::uint64_t m_lastSignaledFence;
};

struct D3d12FrameContext final
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
    D3d12RtvSlot rtv;
    D3d12BackBufferState resourceState;
    std::uint64_t reuseFenceValue;
};

class D3d12PresentationContext final
{
  private:
    std::array<D3d12FrameContext, 2> m_frames;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    D3d12RtvHeap m_rtvHeap;
    D3d12QueueState *m_queueState;
    std::uint64_t m_lastSubmittedFence;
    std::uint32_t m_currentBackBufferIndex;
    D3d12CommandListState m_commandListState;
};
```

`void *m_event`は概念図だけの表現であり、Production実装ではWindows HandleをPrivateなRAII型で一意
所有する。Raw Handleを所有する実装にはしない。正確なClass分割とFile配置はIssue #47、#48、#50で
Coding Rulesと既存D3D12 Backend構造に合わせて決定できるが、所有者とLifetimeは変更しない。

`D3d12BackendDescriptor`にはWindows型を使わず、次のFieldを追加する。

```cpp
std::uint32_t gpuWaitTimeoutMilliseconds;
```

RuntimeHostは5,000を明示し、Factoryは許可範囲をDevice、Queue、Fence生成前に検証する。

## Consequences

### Positive

- Command Allocator ResetとFence完了の対応をFrame Slotごとに一意にできる
- CPUが毎Frame無条件にGPU Idleを待たず、2 Frameを上限に先行できる
- Queue、Fence、EventのNative OwnershipをBackend Private実装へ閉じ込められる
- Swap Chain Index、Frame Context、Back Buffer、RTVの対応を同じIndexへ統一できる
- Resize、Shutdown、Device Removal、Hangの各経路で解放可能範囲を判断できる
- Issue #47から#51を汎用Poolなしで段階的に実装できる

### Negative

- 2 Frames in Flight固定はWorkload別のThroughput調整を行えない
- Presentation ContextごとにCommand Listを一つ持つため、Parallel Recordingを行えない
- 一つのQueue FenceとEventを共有するため、将来のMulti-Thread Waitには再設計が必要になる
- 有限Wait Timeout後は安全性を優先してProcess終端となり、Runtime Recoveryを行わない
- `Unavailable`ではOS回収までNative Resourceを意図的に保持する

## Enforcement

- Issue #47でQueue、Fence、Eventの生成、Signal、Completed Value、有限Wait、Handle解放をTestする
- Issue #47で非terminal Wait Error後の最終完了をFault Injectionし、完了証明後だけ旧Eventを閉じて初期
  未signaledの新Eventへ置換することをTestする。旧Event Closeと新Event生成の各失敗を注入し、二重Close、
  Handle Leak、stale Event再利用がなく、失敗時は`Unavailable`としてまだOpenなHandleとGPU Resourceを
  保持し、正常Close済みHandleを再利用しないことをTestする
- Issue #47のTest Supportで初期`nextFenceValue`を`UINT64_MAX - 1`へ設定し、最後のSignal成功、次回の
  `RHI.FenceValueExhausted`、Fence値の非Wrap／非再利用、Execute前Discard、既存`lastSignaledFence`
  Drain後だけCleanupする経路をTestする
- Issue #48で2 Frame Contextを最低300回周回し、Allocator Resetが対応Fence完了後だけ行われることを
  Testする
- Issue #48で`begin_frame`のEvent登録失敗、Wait Timeout、`WAIT_FAILED`、予期しないWait結果、最終Completed
  ValueとのraceをFault Injectionする。最終完了時はAllocator／ListをResetせず`Submitted`と追跡値を維持し、
  Event置換成功後だけ`Ready`とFrame受付を維持してWait Errorを返し、次回呼出でだけ安全にResetすることを
  Testする。続けて別の未完了Fenceを新Eventで待ち、旧Eventのstale signalで即時復帰しないことまで確認する。
  Event置換失敗と完了／Removal証明不能時は`Unavailable`としてAllocator、Back Buffer、全Context Resource、
  Backend登録を保持し、Device Removal時はResetせずDREDとCleanupへ進むことをTestする
- Issue #48でCommand List `Close`をFault Injectionし、`RecordingCloseFailed`への遷移、新規Frame受付停止、
  Execute／Reset／再Close禁止、Context terminal SignalとWait後の安全な解放をTestする。terminal Signal
  またはWaitも失敗した場合は、予約値または既存terminal値の完了証明とEvent置換成功後にErrorを返しつつ
  安全に解放する経路、Device Removal確認後のDREDとCleanup、Event置換失敗またはどちらも証明不能な場合だけ
  `Unavailable`としてResourceとBackend登録を保持する経路へ分けてTestする
- Command ListのState順序違反とFrame Index範囲外をProcess TestまたはTest Supportで検証する
- Issue #50でSwap Chain Buffer Countが2であり、Current Back Buffer Indexが範囲内であることを検証する
- Issue #51で通常Resize、同一Size、0 Size、Restore、最低50回の連続Resizeを検証する
- Issue #51でFence Timeout、Allocator Reset、Command List Reset／Close、`ResizeBuffers`、Back Buffer
  再取得、RTV再構築を各段階でFault Injectionする。GPU完了未証明時はResourceとBackend登録を保持し、
  GPU Idle確認後の失敗は規定順で解放して登録解除することをTestする
- Issue #47でBackend terminal Signal、Issue #51でContext terminal Signal、Event登録、Wait Timeout、
  `WAIT_FAILED`をFault Injectionする。予約値完了時はBackend terminalがBackendの1値だけ、Context terminalが
  Backendと対象Presentationの2値だけを更新することをTestする。Device Removal時は各対象だけを
  `DeviceRemoved`へ遷移してDREDとCleanupを行い、証明不能時は各対象を`Unavailable`としてResourceと登録を
  保持することをTestする。いずれも対象Frameや存在しないContextの値を更新せず、規定Errorを保持する
- Issue #47と#51でSignal成功後のEvent登録失敗、Wait Timeout、`WAIT_FAILED`、予期しないWait結果、Event後の
  未完了をFault Injectionする。Removal確認後の最終Completed Value取得を必ず実行し、その時点で完了した
  raceでは保存済みterminal値を維持してWait Errorを返しつつ安全にCleanupすること、Device RemovalではDRED
  経路、どちらも証明不能な場合だけ`Unavailable`としてResourceを保持することをTestする。Issue #51のContext
  terminalではCleanup前にEventを置換し、その後に別ContextまたはBackend terminalの未完了Fenceを待って
  stale signalを観測しないことを確認する。Backend terminalだけはEventを再作成せずCleanupする。このWait単独
  失敗へSignal Errorを追加しないことも確認する
- Issue #47、#51、#54でSignal失敗後の完了確認にもWait ErrorをFault Injectionし、最終完了時、Device
  Removal時、証明不能時のすべてで、該当するPresent Error、Signal Error、Wait ErrorがMatrixの優先順位と
  発生順で保持されることをTestする。最終Completed Valueで予約値完了を証明する組み合わせは4種類のSignal
  Originすべてで実行し、Error保持だけでなく、Matrix所定のFence追跡値更新、`Submitted`または`Shutdown`への
  状態遷移、受付停止、Resourceと登録の安全なCleanupまで同じFault Injectionで確認する
- Issue #54でPresentの非Device Removal失敗をFault Injectionし、補完Signal成功時は対象Frameの
  `reuseFenceValue`、Presentation Contextの`lastSubmittedFence`、Backendの`lastSignaledFence`が同じ値へ
  更新されて`Submitted`へ遷移し、新規Frame受付を停止してPresent Errorを返すことをTestする。補完Signalが
  失敗して予約値の完了を証明できた場合も3種類の値を更新して受付停止付き`Submitted`へ遷移し、Present
  ErrorをPrimary、Signal ErrorをSecondaryとして安全なShutdownを行うことをTestする。完了もRemovalも
  証明できない場合は3種類の値を更新せず`ExecutedUnfenced`から`Unavailable`へ遷移し、両Errorと全Resourceを
  保持することをTestする。Device Removalだけを確認できた場合は3種類の値を更新せず`DeviceRemoved`へ遷移し、
  `RHI.DeviceRemoved`、Removal Reason、Present Error、Signal Errorを保持してDREDとFence Waitなしの逆順
  Cleanupを行うことをTestする
- Issue #54でPresent成功後の通常SignalもFault Injectionする。失敗後に予約値の完了を証明できる場合は
  3種類のFence値を更新し、受付停止付き`Submitted`へ遷移して元のErrorを返し、安全なShutdownを行うことを
  Testする。Device Removalを確認した場合は3種類の値を更新せず`RHI.DeviceRemoved`、Removal Reason、
  Signal Errorを保持してDREDとCleanupを行うことをTestする。どちらも証明できない場合は3種類の値を更新せず
  `Unavailable`へ遷移して全Resourceを保持することをTestする
- Debug、Development、ReleaseでHardwareとWARPのSmoke Testを実行する
- Debug LayerとInfoQueueにAllocator Reset、Command List、Resource Lifetime Errorがないことを確認する
- `Cue.RHI`公開Header Compile TestでWindows、DXGI、D3D12型が露出しないことを維持する

## Sources

- [Microsoft: Creating and recording command lists and bundles](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
- [Microsoft: ID3D12CommandAllocator::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset)
- [Microsoft: Executing and synchronizing command lists](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists)
- [Microsoft: ID3D12Fence::SetEventOnCompletion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-seteventoncompletion)
- [Microsoft: ID3D12Fence::GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)
- [Microsoft: Multi-engine synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization)
- [Microsoft: Swap chains](https://learn.microsoft.com/en-us/windows/win32/direct3d12/swap-chains)
- [Microsoft: Use DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
- [Microsoft: IDXGISwapChain::ResizeBuffers](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)
- [Unreal Engine: RHI](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI)
- [Unreal Engine: Parallel Rendering Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine)
- [Unity: GraphicsFence](https://docs.unity3d.com/ScriptReference/Rendering.GraphicsFence.html)
- [Godot: RenderingDevice](https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html)

## Follow-up

- Issue #47でDirect Graphics Queue、Fence、Event、単調Fence値、有限Waitを実装する
- Issue #48で2個のFrame Context、Allocator再利用Fence、Graphics Command List Stateを実装する
- Issue #49で2 SlotのRTV Descriptor Heap Ownershipを実装する
- Issue #50で2 Buffer Flip Discard Swap Chain、Back Buffer、Current Indexを実装する
- Issue #51でBack Buffer RTV再構築、Resize、Minimize、Shutdown順序を実装する
- M05でBack Buffer Resource Barrier、Clear、Present Frame Loopを追加する
