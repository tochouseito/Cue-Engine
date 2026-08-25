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

[[nodiscard]] const D3d12QueueNativeFunctions &default_d3d12_queue_native_functions() noexcept;

// Win32 Event Handle の一意所有を表し、待機失敗時の Handle 交換を安全に行う
class D3d12FenceEvent final
{
  public:
    D3d12FenceEvent() noexcept;
    D3d12FenceEvent(HANDLE a_handle, BOOL(WINAPI *a_closeHandle)(HANDLE)) noexcept;
    D3d12FenceEvent(const D3d12FenceEvent &) = delete;
    D3d12FenceEvent &operator=(const D3d12FenceEvent &) = delete;
    D3d12FenceEvent(D3d12FenceEvent &&a_other) noexcept;
    D3d12FenceEvent &operator=(D3d12FenceEvent &&) noexcept = delete;
    ~D3d12FenceEvent() noexcept;

    [[nodiscard]] HANDLE get() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    void reset(HANDLE a_handle, BOOL(WINAPI *a_closeHandle)(HANDLE)) noexcept;
    void mark_closed() noexcept;

  private:
    friend class D3d12QueueState;

    void take_from(D3d12FenceEvent &&a_other) noexcept;

    HANDLE m_handle;
    BOOL(WINAPI *m_closeHandle)(HANDLE);
};

class D3d12QueueState final
{
  public:
    D3d12QueueState(const D3d12QueueState &) = delete;
    D3d12QueueState &operator=(const D3d12QueueState &) = delete;
    D3d12QueueState(D3d12QueueState &&) noexcept = default;
    D3d12QueueState &operator=(D3d12QueueState &&a_other) noexcept;
    ~D3d12QueueState() noexcept = default;

    [[nodiscard]] Result<std::uint64_t> reserve_fence_value() noexcept;
    void execute_command_list(ID3D12CommandList *a_commandList) noexcept;
    [[nodiscard]] Result<void> signal_reserved(std::uint64_t a_fenceValue) noexcept;
    [[nodiscard]] Result<void> reclassify_device_failure(Error &&a_error) noexcept;
    [[nodiscard]] bool refresh_device_removed_status() noexcept;
    [[nodiscard]] Result<void> resolve_failed_signal(std::uint64_t a_fenceValue, Error &&a_signalError,
                                                     D3d12FenceWaitPurpose a_purpose) noexcept;
    [[nodiscard]] std::uint64_t completed_value() noexcept;
    [[nodiscard]] Result<void> wait_for_fence(std::uint64_t a_fenceValue, D3d12FenceWaitPurpose a_purpose) noexcept;
    [[nodiscard]] Result<void> shutdown() noexcept;
    [[nodiscard]] Result<void> release_after_device_removed() noexcept;

    [[nodiscard]] D3d12QueueStateStatus status() const noexcept;
    [[nodiscard]] std::uint64_t next_fence_value() const noexcept;
    [[nodiscard]] std::uint64_t last_signaled_fence() const noexcept;
    [[nodiscard]] bool was_last_wait_completion_proven() const noexcept;
    [[nodiscard]] bool has_native_objects() const noexcept;
    [[nodiscard]] bool has_gpu_objects() const noexcept;
    [[nodiscard]] bool has_queue() const noexcept;
    [[nodiscard]] bool has_fence() const noexcept;
    [[nodiscard]] bool has_fence_event() const noexcept;
    [[nodiscard]] ID3D12CommandQueue *native_queue_for_presentation() const noexcept;

    void set_next_fence_value_for_test(std::uint64_t a_nextFenceValue) noexcept;

  private:
    friend Result<D3d12QueueState> create_d3d12_queue_state(ID3D12Device *, std::uint32_t, const AssertContext &,
                                                            const D3d12QueueNativeFunctions &) noexcept;

    D3d12QueueState(Microsoft::WRL::ComPtr<ID3D12CommandQueue> a_queue, Microsoft::WRL::ComPtr<ID3D12Fence> a_fence,
                    D3d12FenceEvent &&a_event, ID3D12Device *a_device, std::uint32_t a_waitTimeoutMilliseconds,
                    const D3d12QueueNativeFunctions &a_functions, const AssertContext &a_assertContext) noexcept;

    [[nodiscard]] Result<void> resolve_abnormal_wait(Error &&a_waitError, std::uint64_t a_fenceValue,
                                                     D3d12FenceWaitPurpose a_purpose) noexcept;
    [[nodiscard]] Result<void> replace_wait_event(Error &&a_cause) noexcept;
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
