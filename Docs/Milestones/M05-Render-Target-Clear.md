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
- `Resized`と`Restored`は一度のEvent処理で最新Client Sizeへ集約し、Frame境界でResizeする。
- `Minimized`では0 Size ResizeでFrame受付を停止し、ClearとPresentを行わない。
- `DXGI_STATUS_OCCLUDED`は非致命の表示結果として扱い、描画Loopを継続しながら待機する。
- 終了時はPresentation、Backend、Windowの順に停止・破棄する。

## Resizeと停止

Window Message CallbackはEventをFIFOへ格納するだけで、GPU待機を行わない。RuntimeHostはEventをすべて
取り出し、同一Batchでは最後に観測したMinimize、Resize、Restoreを最新Window状態として採用する。その後、
Close、最新Window状態の適用、Presentの優先順で処理する。CloseとResizeが同時に届いた場合は新しいGPU
WorkやResizeを開始せず停止へ進む。

非0 SizeのNative ResizeとShutdown前に必要なGPU完了は、Presentation内部の有限時間Fence Waitで証明する。
MinimizeはResourceを保持したままFrame受付だけを停止してNative `ResizeBuffers`を呼ばない。Restore後に
Sizeが変わっていればGPU完了後に最新の非0 Client SizeでBack BufferとRTVを再構築し、同一Sizeなら受付を
再開して次のFrame Context再利用時に必要なFenceを待つ。ShutdownはBack Buffer、Allocator、RTV Heap、
Swap ChainをPresentation側で解放してからBackendのQueue、Fence、Deviceを解放する。

RuntimeHostのResize Smokeは、Window Callbackを経由して50サイクルの連続Resize、Minimize、Restoreを
実行する。各サイクルの連続Resizeは最新Sizeへ集約し、Minimize中の描画停止、Restore後のClear再開、
InfoQueue Errorなし、正常Shutdownを検証する。最後は連続ResizeとCloseを同一Event Batchへ格納し、
Closeを優先してPresentation SizeとResize適用回数が変わらないことを確認する。

Frame同期とPresent Error後の補完Signalの正式契約は
[ADR-0008](../Decisions/0008-frame-flight-gpu-synchronization-ownership.md)を正本とする。
