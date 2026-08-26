// Swap Chain Back Buffer 専用の RTV Descriptor Heap と固定 Slot を所有する内部 Allocator
// Heap 世代と Slot 世代を Handle へ含め、Resize 前の古い Slot を新しい Heap へ誤使用しないようにする

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

/// @brief Production 経路で使用する D3D12 Native API 関数 Table を構築して返す
[[nodiscard]] const D3d12RtvHeapNativeFunctions &default_d3d12_rtv_heap_native_functions() noexcept;

class D3d12RtvHeap final
{
  public:
    /// @brief D3d12RtvHeap の一意所有を保つため Copy 構築を禁止する
    D3d12RtvHeap(const D3d12RtvHeap &) = delete;
    /// @brief D3d12RtvHeap の一意所有を保つため Copy 代入を禁止する
    D3d12RtvHeap &operator=(const D3d12RtvHeap &) = delete;
    /// @brief D3d12RtvHeap の所有状態を Move 構築し、移動元を安全な空状態へ戻す
    D3d12RtvHeap(D3d12RtvHeap &&a_other) noexcept;
    /// @brief D3d12RtvHeap の所有状態を Move 代入し、代入元を安全な空状態へ移す
    D3d12RtvHeap &operator=(D3d12RtvHeap &&a_other) noexcept;
    /// @brief D3d12RtvHeap が保持する Resource を所有権規則に従って破棄する
    ~D3d12RtvHeap() noexcept;

    /// @brief Allocation 経路の設定に従って Memory を確保し、確保結果を返す
    [[nodiscard]] Result<D3d12RtvSlot> allocate() noexcept;
    /// @brief 保持する Native Resource を完了条件と所有権規則に従って解放する
    [[nodiscard]] Result<void> release(D3d12RtvSlot a_slot) noexcept;
    /// @brief D3D12 RTV Heap が保持する CPU Handle を呼び出し元へ返す
    [[nodiscard]] Result<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handle(D3d12RtvSlot a_slot) const noexcept;
    /// @brief 保持する Native Resource を依存関係と完了条件に従って停止し、安全な解放結果を返す
    [[nodiscard]] Result<void> shutdown() noexcept;

    /// @brief D3D12 RTV Heap が保持する Capacity を呼び出し元へ返す
    [[nodiscard]] std::uint32_t capacity() const noexcept;
    /// @brief D3D12 RTV Heap が保持する Used Count を呼び出し元へ返す
    [[nodiscard]] std::uint32_t used_count() const noexcept;
    /// @brief D3D12 RTV Heap が保持する Descriptor Increment Size を呼び出し元へ返す
    [[nodiscard]] std::uint32_t descriptor_increment_size() const noexcept;
    /// @brief D3D12 RTV Heap の Native Object 条件を判定して返す
    [[nodiscard]] bool has_native_object() const noexcept;

    /// @brief D3D12 RTV Heap の Slot Generation For Test を整合性を保って更新する
    void set_slot_generation_for_test(std::uint32_t a_slotIndex, std::uint64_t a_generation) noexcept;

  private:
    /// @brief D3D12 RTV Heap で使用する D3D12 RTV Heap を生成し、呼び出し元へ返す
    friend Result<D3d12RtvHeap> create_d3d12_rtv_heap(ID3D12Device *, const AssertContext &,
                                                      const D3d12RtvHeapNativeFunctions &,
                                                      const D3d12RtvHeapFailureHandler &) noexcept;

    /// @brief D3d12RtvHeap を必要な依存と初期状態から構築する
    D3d12RtvHeap(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> a_heap,
                 std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_d3d12RtvDescriptorCapacity> a_handles,
                 std::uint32_t a_incrementSize, std::uint64_t a_incarnation,
                 const AssertContext &a_assertContext) noexcept;

    /// @brief 移動元の Native 所有権と Lifecycle 状態を受け取り、移動元を安全な空状態へ戻す
    void take_from(D3d12RtvHeap &&a_other) noexcept;
    /// @brief D3D12 RTV Heap の Live Slot 条件を判定して返す
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
