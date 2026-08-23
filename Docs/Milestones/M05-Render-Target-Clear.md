# M05: Render Target Clear

M05は、Windowに対応するD3D12 Back Bufferを固定色でClearし、Submit、Present、安全な停止までを
RuntimeHostから一貫して実行できる最小描画経路を確立する。

## Frame契約

`PresentationContext::present_frame()`は、Platform非依存のClear Colorを受け取り、次の順序を一つの
生成Thread限定操作として実行する。

1. Current Back Buffer Indexに対応するFrame Contextの再利用Fence完了を確認する。
2. AllocatorとCommand ListをResetする。
3. Back Bufferを`PRESENT`から`RENDER_TARGET`へ遷移する。
4. 固定色でClearし、`PRESENT`へ戻してCommand ListをClose、Executeする。
5. `IDXGISwapChain3::Present`を試行する。
6. Execute済みWorkを覆うFenceをSignalしてFrame Contextへ保存する。
7. Present成功後のCurrent Back Buffer Indexを取得する。

公開RHI APIはNative Fence値、DXGI型、D3D12型を公開しない。Fence値とFrame Contextの対応はD3D12
TestSupportで診断し、公開APIは`Presented`または`Occluded`だけを返す。

## RuntimeHost責務

- VSyncはRuntimeHostが既定で有効にする。
- Window Eventを処理して停止要求を確認した後だけ、新しいFrameを開始する。
- 終了時はPresentation、Backend、Windowの順に停止・破棄する。
- Resize、Minimize、Restoreの描画連携はIssue #55で追加する。

Frame同期とPresent Error後の補完Signalの正式契約は
[ADR-0008](../Decisions/0008-frame-flight-gpu-synchronization-ownership.md)を正本とする。
