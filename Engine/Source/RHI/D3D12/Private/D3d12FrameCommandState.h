#pragma once

#include <Cue/Foundation/Result.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace cue
{
class AssertContext;
class D3d12QueueState;

constexpr std::uint32_t k_d3d12FrameContextCount = 2;

enum class D3d12CommandListState
{
    Initial,
    IdleClosed,
    Submitted,
    FrameResetFailed,
    Recording,
    RecordingCloseFailed,
    Closed,
    ExecutedAwaitingPresent,
    ExecutedUnfenced,
};

enum class D3d12FrameCommandStatus
{
    Ready,
    DeviceRemoved,
    Unavailable,
    Shutdown,
};

struct D3d12FrameContext final
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    std::uint64_t reuseFenceValue;
};

struct D3d12FrameCommandNativeFunctions final
{
    HRESULT (*createCommandAllocator)(ID3D12Device *, ID3D12CommandAllocator **) noexcept;
    HRESULT (*createCommandList)(ID3D12Device *, ID3D12CommandAllocator *, ID3D12GraphicsCommandList **) noexcept;
    HRESULT (*setObjectName)(ID3D12Object *, LPCWSTR) noexcept;
    HRESULT (*resetCommandAllocator)(ID3D12CommandAllocator *) noexcept;
    HRESULT (*resetCommandList)(ID3D12GraphicsCommandList *, ID3D12CommandAllocator *) noexcept;
    HRESULT (*closeCommandList)(ID3D12GraphicsCommandList *) noexcept;
};

[[nodiscard]] const D3d12FrameCommandNativeFunctions &default_d3d12_frame_command_native_functions() noexcept;

class D3d12FrameCommandState final
{
  public:
    D3d12FrameCommandState(const D3d12FrameCommandState &) = delete;
    D3d12FrameCommandState &operator=(const D3d12FrameCommandState &) = delete;
    D3d12FrameCommandState(D3d12FrameCommandState &&a_other) noexcept;
    D3d12FrameCommandState &operator=(D3d12FrameCommandState &&a_other) noexcept;
    ~D3d12FrameCommandState() noexcept;

    [[nodiscard]] Result<void> begin_frame(std::uint32_t a_frameIndex) noexcept;
    [[nodiscard]] Result<void> close_frame() noexcept;
    [[nodiscard]] Result<void> execute_frame() noexcept;
    [[nodiscard]] Result<void> mark_present_attempted() noexcept;
    [[nodiscard]] Result<std::uint64_t> signal_frame() noexcept;
    [[nodiscard]] Result<void> shutdown() noexcept;
    [[nodiscard]] Result<void> release_after_device_removed() noexcept;

    [[nodiscard]] D3d12CommandListState command_list_state() const noexcept;
    [[nodiscard]] D3d12FrameCommandStatus status() const noexcept;
    [[nodiscard]] std::uint64_t frame_reuse_fence(std::uint32_t a_frameIndex) const noexcept;
    [[nodiscard]] std::uint64_t last_submitted_fence() const noexcept;
    [[nodiscard]] bool is_accepting_frames() const noexcept;
    [[nodiscard]] bool has_native_objects() const noexcept;

  private:
    friend Result<D3d12FrameCommandState> create_d3d12_frame_command_state(
        ID3D12Device *, D3d12QueueState &, const AssertContext &, const D3d12FrameCommandNativeFunctions &) noexcept;

    D3d12FrameCommandState(std::array<D3d12FrameContext, k_d3d12FrameContextCount> &&a_frames,
                           Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> a_commandList,
                           D3d12QueueState &a_queueState, const AssertContext &a_assertContext,
                           const D3d12FrameCommandNativeFunctions &a_functions) noexcept;

    void take_from(D3d12FrameCommandState &&a_other) noexcept;
    void update_status_from_queue() noexcept;
    void release_native_objects() noexcept;
    [[nodiscard]] Result<void> discard_closed_frame_after_exhaustion() noexcept;
    [[nodiscard]] Result<void> handle_reset_failure(Error &&a_error) noexcept;
    [[nodiscard]] Result<void> handle_close_failure(Error &&a_error) noexcept;

    std::array<D3d12FrameContext, k_d3d12FrameContextCount> m_frames;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    D3d12QueueState *m_queueState;
    const AssertContext *m_assertContext;
    D3d12FrameCommandNativeFunctions m_functions;
    std::uint64_t m_lastSubmittedFence;
    std::uint64_t m_pendingFenceValue;
    std::uint32_t m_activeFrameIndex;
    D3d12CommandListState m_commandListState;
    D3d12FrameCommandStatus m_status;
    bool m_acceptingFrames;
};

[[nodiscard]] Result<D3d12FrameCommandState> create_d3d12_frame_command_state(
    ID3D12Device *a_device, D3d12QueueState &a_queueState, const AssertContext &a_assertContext,
    const D3d12FrameCommandNativeFunctions &a_functions = default_d3d12_frame_command_native_functions()) noexcept;
} // namespace cue
