#pragma once

#include <Cue/Foundation/Result.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace cue
{
class AssertContext;
class Error;

constexpr std::uint32_t k_d3d12RtvDescriptorCapacity = 2;

struct D3d12RtvSlot final
{
    std::uint64_t heapIncarnation;
    std::uint32_t index;
    std::uint64_t generation;
};

struct D3d12RtvHeapNativeFunctions final
{
    HRESULT (*createDescriptorHeap)(ID3D12Device *, const D3D12_DESCRIPTOR_HEAP_DESC *,
                                    ID3D12DescriptorHeap **) noexcept;
    UINT (*getDescriptorHandleIncrementSize)(ID3D12Device *) noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE (*getCpuDescriptorHandleForHeapStart)(ID3D12DescriptorHeap *) noexcept;
    HRESULT (*setObjectName)(ID3D12Object *, LPCWSTR) noexcept;
};

struct D3d12RtvHeapFailureResources final
{
    ID3D12DescriptorHeap *heap;
};

struct D3d12RtvHeapFailureHandler final
{
    void *owner;
    Result<void> (*handleNativeFailure)(void *, Error &&, const D3d12RtvHeapFailureResources &) noexcept;
};

[[nodiscard]] const D3d12RtvHeapNativeFunctions &default_d3d12_rtv_heap_native_functions() noexcept;

class D3d12RtvHeap final
{
  public:
    D3d12RtvHeap(const D3d12RtvHeap &) = delete;
    D3d12RtvHeap &operator=(const D3d12RtvHeap &) = delete;
    D3d12RtvHeap(D3d12RtvHeap &&a_other) noexcept;
    D3d12RtvHeap &operator=(D3d12RtvHeap &&a_other) noexcept;
    ~D3d12RtvHeap() noexcept;

    [[nodiscard]] Result<D3d12RtvSlot> allocate() noexcept;
    [[nodiscard]] Result<void> release(D3d12RtvSlot a_slot) noexcept;
    [[nodiscard]] Result<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handle(D3d12RtvSlot a_slot) const noexcept;
    [[nodiscard]] Result<void> shutdown() noexcept;

    [[nodiscard]] std::uint32_t capacity() const noexcept;
    [[nodiscard]] std::uint32_t used_count() const noexcept;
    [[nodiscard]] std::uint32_t descriptor_increment_size() const noexcept;
    [[nodiscard]] bool has_native_object() const noexcept;

    void set_slot_generation_for_test(std::uint32_t a_slotIndex, std::uint64_t a_generation) noexcept;

  private:
    friend Result<D3d12RtvHeap> create_d3d12_rtv_heap(ID3D12Device *, const AssertContext &,
                                                      const D3d12RtvHeapNativeFunctions &,
                                                      const D3d12RtvHeapFailureHandler &) noexcept;

    D3d12RtvHeap(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> a_heap,
                 std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_d3d12RtvDescriptorCapacity> a_handles,
                 std::uint32_t a_incrementSize, std::uint64_t a_incarnation,
                 const AssertContext &a_assertContext) noexcept;

    void take_from(D3d12RtvHeap &&a_other) noexcept;
    [[nodiscard]] bool is_live_slot(D3d12RtvSlot a_slot) const noexcept;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_d3d12RtvDescriptorCapacity> m_handles;
    std::array<std::uint64_t, k_d3d12RtvDescriptorCapacity> m_generations;
    std::array<bool, k_d3d12RtvDescriptorCapacity> m_allocated;
    const AssertContext *m_assertContext;
    std::uint32_t m_incrementSize;
    std::uint32_t m_usedCount;
    std::uint64_t m_incarnation;
};

[[nodiscard]] Result<D3d12RtvHeap> create_d3d12_rtv_heap(
    ID3D12Device *a_device, const AssertContext &a_assertContext,
    const D3d12RtvHeapNativeFunctions &a_functions = default_d3d12_rtv_heap_native_functions(),
    const D3d12RtvHeapFailureHandler &a_failureHandler = {}) noexcept;
} // namespace cue
