// Direct Command Queue、Fence、待機 Event を一つの Lifetime で所有し、GPU 完了を証明する内部状態
// 通常終了で完了を証明できない場合は Resource を保持して Unavailable へ移行する
// Device Removal 時は DeviceRemoved へ移行し、呼出側が利用可能な診断を試行した後に専用経路で解放する

#pragma once

#include <Cue/Foundation/Result.h>

#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>

namespace cue
{
class AssertContext;

enum class D3d12FenceWaitPurpose
{
    // Frame Resource 再利用のための通常待機
    Reusable,
    // Backend 終了時に全 GPU Work 完了を証明する待機
    BackendTerminal,
};

enum class D3d12QueueStateStatus
{
    Ready,
    DeviceRemoved,
    Unavailable,
    Shutdown,
};

struct D3d12QueueNativeFunctions final
{
    void (*executeCommandLists)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *) noexcept;
    HRESULT (*signal)(ID3D12CommandQueue *, ID3D12Fence *, std::uint64_t) noexcept;
    std::uint64_t (*getCompletedValue)(ID3D12Fence *) noexcept;
    HRESULT (*setEventOnCompletion)(ID3D12Fence *, std::uint64_t, HANDLE) noexcept;
    DWORD(WINAPI *waitForSingleObject)(HANDLE, DWORD);
    HANDLE(WINAPI *createEvent)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
    BOOL(WINAPI *closeHandle)(HANDLE);
    DWORD(WINAPI *getLastError)();
    HRESULT (*getDeviceRemovedReason)(ID3D12Device *) noexcept;
};

/// @brief Production 経路で使用する D3D12 Native API 関数 Table を構築して返す
[[nodiscard]] const D3d12QueueNativeFunctions &default_d3d12_queue_native_functions() noexcept;

// Win32 Event Handle の一意所有を表し、待機失敗時の Handle 交換を安全に行う
class D3d12FenceEvent final
{
  public:
    /// @brief Event をまだ所有しない空の Fence Event Owner を構築する
    D3d12FenceEvent() noexcept;
    /// @brief Native Event Handle と Close Callback の一意所有を開始する
    D3d12FenceEvent(HANDLE a_handle, BOOL(WINAPI *a_closeHandle)(HANDLE)) noexcept;
    /// @brief D3d12FenceEvent の一意所有を保つため Copy 構築を禁止する
    D3d12FenceEvent(const D3d12FenceEvent &) = delete;
    /// @brief D3d12FenceEvent の一意所有を保つため Copy 代入を禁止する
    D3d12FenceEvent &operator=(const D3d12FenceEvent &) = delete;
    /// @brief Native Event Handle の一意所有を移動元から引き継ぐ
    D3d12FenceEvent(D3d12FenceEvent &&a_other) noexcept;
    /// @brief D3d12FenceEvent の所有状態を移動させないため Move 代入を禁止する
    D3d12FenceEvent &operator=(D3d12FenceEvent &&) noexcept = delete;
    /// @brief D3d12FenceEvent が保持する Resource を所有権規則に従って破棄する
    ~D3d12FenceEvent() noexcept;

    /// @brief 所有中の Native Event HANDLE を所有権を移さない参照値として返す
    [[nodiscard]] HANDLE get() const noexcept;
    /// @brief D3D12 Queue State の Open 条件を判定して返す
    [[nodiscard]] bool is_open() const noexcept;
    /// @brief 新しい Native Event HANDLE と Close Callback を受け取り、Event の一意所有を開始する
    void reset(HANDLE a_handle, BOOL(WINAPI *a_closeHandle)(HANDLE)) noexcept;
    /// @brief 呼び出し側で閉じた Native Event の Handle を空にし、Event を非所有状態へ戻す
    void mark_closed() noexcept;

  private:
    friend class D3d12QueueState;

    /// @brief 移動元の Native 所有権と Lifecycle 状態を受け取り、移動元を安全な空状態へ戻す
    void take_from(D3d12FenceEvent &&a_other) noexcept;

    HANDLE m_handle;
    BOOL(WINAPI *m_closeHandle)(HANDLE);
};

class D3d12QueueState final
{
  public:
    /// @brief D3d12QueueState の一意所有を保つため Copy 構築を禁止する
    D3d12QueueState(const D3d12QueueState &) = delete;
    /// @brief D3d12QueueState の一意所有を保つため Copy 代入を禁止する
    D3d12QueueState &operator=(const D3d12QueueState &) = delete;
    /// @brief Queue State の所有状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    D3d12QueueState(D3d12QueueState &&) noexcept = default;
    /// @brief D3d12QueueState の所有状態を Move 代入し、代入元を安全な空状態へ移す
    D3d12QueueState &operator=(D3d12QueueState &&a_other) noexcept;
    /// @brief D3d12QueueState が保持する Resource を所有権規則に従って破棄する
    ~D3d12QueueState() noexcept = default;

    /// @brief 未処理予約がないことを検証して次の Fence 値を予約し、値の枯渇時は Error を返す
    [[nodiscard]] Result<std::uint64_t> reserve_fence_value() noexcept;
    /// @brief D3D12 Queue State の Command List を GPU 実行順と Resource State を守って投入する
    void execute_command_list(ID3D12CommandList *a_commandList) noexcept;
    /// @brief D3D12 Queue State の Reserved へ完了通知を発行し、追跡する Fence 値を確定する
    [[nodiscard]] Result<void> signal_reserved(std::uint64_t a_fenceValue) noexcept;
    /// @brief D3D12 Queue State の Device Failure を既存の診断情報を失わず追加または再分類する
    [[nodiscard]] Result<void> reclassify_device_failure(Error &&a_error) noexcept;
    /// @brief Native Device Removal Reason を再取得し、除去検出時は状態を DeviceRemoved へ遷移して true を返す
    [[nodiscard]] bool refresh_device_removed_status() noexcept;
    /// @brief Queue Signal 失敗後の Completion と Device Removal を再確認して終端状態を確定する
    [[nodiscard]] Result<void> resolve_failed_signal(std::uint64_t a_fenceValue, Error &&a_signalError,
                                                     D3d12FenceWaitPurpose a_purpose) noexcept;
    /// @brief D3D12 Queue State が保持する Completed Value を呼び出し元へ返す
    [[nodiscard]] std::uint64_t completed_value() noexcept;
    /// @brief 指定 Fence を有限時間待機し、完了証明を更新するか異常内容に応じて終端状態へ遷移する
    [[nodiscard]] Result<void> wait_for_fence(std::uint64_t a_fenceValue, D3d12FenceWaitPurpose a_purpose) noexcept;
    /// @brief 保持する Native Resource を依存関係と完了条件に従って停止し、安全な解放結果を返す
    [[nodiscard]] Result<void> shutdown() noexcept;
    /// @brief D3D12 Queue State の After Device Removed を依存関係と完了条件を守って安全に解放または停止する
    [[nodiscard]] Result<void> release_after_device_removed() noexcept;

    /// @brief D3D12 Queue State が保持する Status を呼び出し元へ返す
    [[nodiscard]] D3d12QueueStateStatus status() const noexcept;
    /// @brief D3D12 Queue State が保持する Next Fence Value を呼び出し元へ返す
    [[nodiscard]] std::uint64_t next_fence_value() const noexcept;
    /// @brief D3D12 Queue State が保持する Last Signaled Fence を呼び出し元へ返す
    [[nodiscard]] std::uint64_t last_signaled_fence() const noexcept;
    /// @brief 直前の Fence 待機で GPU 完了が証明されたかを返す
    [[nodiscard]] bool was_last_wait_completion_proven() const noexcept;
    /// @brief 安全な解放判断に必要な Native Object が残存しているかを返す
    [[nodiscard]] bool has_native_objects() const noexcept;
    /// @brief D3D12 Queue State の GPU Objects 条件を判定して返す
    [[nodiscard]] bool has_gpu_objects() const noexcept;
    /// @brief D3D12 Queue State の Queue 条件を判定して返す
    [[nodiscard]] bool has_queue() const noexcept;
    /// @brief D3D12 Queue State の Fence 条件を判定して返す
    [[nodiscard]] bool has_fence() const noexcept;
    /// @brief D3D12 Queue State の Fence Event 条件を判定して返す
    [[nodiscard]] bool has_fence_event() const noexcept;
    /// @brief Presentation 生成へ渡す非所有 D3D12 Command Queue を返す
    [[nodiscard]] ID3D12CommandQueue *native_queue_for_presentation() const noexcept;

    /// @brief D3D12 Queue State の Next Fence Value For Test を整合性を保って更新する
    void set_next_fence_value_for_test(std::uint64_t a_nextFenceValue) noexcept;

  private:
    /// @brief D3D12 Queue State で使用する D3D12 Queue State を生成し、呼び出し元へ返す
    friend Result<D3d12QueueState> create_d3d12_queue_state(ID3D12Device *, std::uint32_t, const AssertContext &,
                                                            const D3d12QueueNativeFunctions &) noexcept;

    /// @brief D3d12QueueState を必要な依存と初期状態から構築する
    D3d12QueueState(Microsoft::WRL::ComPtr<ID3D12CommandQueue> a_queue, Microsoft::WRL::ComPtr<ID3D12Fence> a_fence,
                    D3d12FenceEvent &&a_event, ID3D12Device *a_device, std::uint32_t a_waitTimeoutMilliseconds,
                    const D3d12QueueNativeFunctions &a_functions, const AssertContext &a_assertContext) noexcept;

    /// @brief Fence 待機の異常結果を Completion、Device Removal、Unavailable へ分類する
    [[nodiscard]] Result<void> resolve_abnormal_wait(Error &&a_waitError, std::uint64_t a_fenceValue,
                                                     D3d12FenceWaitPurpose a_purpose) noexcept;
    /// @brief D3D12 Queue State の Wait Event を整合性を保って更新する
    [[nodiscard]] Result<void> replace_wait_event(Error &&a_cause) noexcept;
    /// @brief D3D12 Queue State の Native Objects を依存関係と完了条件を守って安全に解放または停止する
    [[nodiscard]] Result<void> release_native_objects() noexcept;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    D3d12FenceEvent m_event;
    ID3D12Device *m_device;
    std::uint64_t m_nextFenceValue;
    std::uint64_t m_lastAttemptedFence;
    std::uint64_t m_lastSignaledFence;
    std::uint32_t m_waitTimeoutMilliseconds;
    D3d12QueueNativeFunctions m_functions;
    const AssertContext *m_assertContext;
    D3d12QueueStateStatus m_status;
    bool m_lastWaitCompletionProven;
};

[[nodiscard]] Result<D3d12QueueState> create_d3d12_queue_state(
    ID3D12Device *a_device, std::uint32_t a_waitTimeoutMilliseconds, const AssertContext &a_assertContext,
    const D3d12QueueNativeFunctions &a_functions = default_d3d12_queue_native_functions()) noexcept;
} // namespace cue
