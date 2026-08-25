// Back Buffer 数だけ RTV Slot を払い出し、解放済みまたは別 Heap 由来の Slot 利用を拒否する
// Shutdown 時に使用中 Slot が残る場合は Heap を保持し、参照中 Descriptor の破棄を防ぐ

#include "D3d12RtvHeap.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <atomic>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
constexpr std::int64_t k_heapCreationFailed = 65;
constexpr std::int64_t k_heapNameFailed = 66;
constexpr std::int64_t k_invalidIncrementSize = 67;
constexpr std::int64_t k_capacityExhausted = 68;
constexpr std::int64_t k_invalidSlotIndex = 69;
constexpr std::int64_t k_inactiveSlot = 70;
constexpr std::int64_t k_handleOverflow = 71;
constexpr std::int64_t k_liveSlotsAtShutdown = 72;
constexpr std::int64_t k_invalidDevice = 73;
constexpr std::int64_t k_heapShutdown = 74;
constexpr std::int64_t k_heapIncarnationExhausted = 75;
constexpr std::int64_t k_slotGenerationExhausted = 76;

std::atomic<std::uint64_t> g_nextHeapIncarnation = 1;

// Process 内で再生成された Heap へ同じ識別子を再利用せず、古い Slot との取り違えを防ぐ
[[nodiscard]] std::uint64_t reserve_heap_incarnation() noexcept
{
    std::uint64_t candidate = g_nextHeapIncarnation.load(std::memory_order_relaxed);

    while (candidate != (std::numeric_limits<std::uint64_t>::max)())
    {
        if (g_nextHeapIncarnation.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed))
        {
            return candidate;
        }
    }

    return 0;
}

[[nodiscard]] cue::ErrorCode make_code(const cue::AssertContext &a_context, std::int64_t a_value) noexcept
{
    return cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_value);
}

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary) noexcept
{
    return cue::Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary);
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, HRESULT a_nativeCode) noexcept
{
    cue::NativeError nativeError =
        cue::NativeError::create(a_context.fatal_handler(), "D3D12", static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary,
                              std::move(nativeError));
}

[[nodiscard]] cue::Result<cue::D3d12RtvHeap> fail_native_creation(
    cue::Error &&a_error, const cue::D3d12RtvHeapFailureHandler &a_failureHandler, ID3D12DescriptorHeap *a_heap,
    const cue::AssertContext &a_assertContext) noexcept
{
    if (a_failureHandler.handleNativeFailure == nullptr)
    {
        return cue::Result<cue::D3d12RtvHeap>::failure(std::move(a_error));
    }

    cue::D3d12RtvHeapFailureResources resources = {a_heap};
    cue::Result<void> handlerResult =
        a_failureHandler.handleNativeFailure(a_failureHandler.owner, std::move(a_error), resources);

    if (handlerResult)
    {
        a_assertContext.fatal_handler().terminate("D3D12 RTV Heap failure handler did not retain an Error");
    }

    return cue::Result<cue::D3d12RtvHeap>::failure(std::move(*handlerResult.try_error()));
}

HRESULT create_descriptor_heap(ID3D12Device *a_device, const D3D12_DESCRIPTOR_HEAP_DESC *a_descriptor,
                               ID3D12DescriptorHeap **a_heap) noexcept
{
    return a_device->CreateDescriptorHeap(a_descriptor, IID_PPV_ARGS(a_heap));
}

UINT get_descriptor_handle_increment_size(ID3D12Device *a_device) noexcept
{
    return a_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_descriptor_handle_for_heap_start(ID3D12DescriptorHeap *a_heap) noexcept
{
    return a_heap->GetCPUDescriptorHandleForHeapStart();
}

HRESULT set_object_name(ID3D12Object *a_object, LPCWSTR a_name) noexcept
{
    return a_object->SetName(a_name);
}
} // namespace

namespace cue
{
const D3d12RtvHeapNativeFunctions &default_d3d12_rtv_heap_native_functions() noexcept
{
    static const D3d12RtvHeapNativeFunctions functions = {
        create_descriptor_heap,
        get_descriptor_handle_increment_size,
        get_cpu_descriptor_handle_for_heap_start,
        set_object_name,
    };
    return functions;
}

D3d12RtvHeap::D3d12RtvHeap(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> a_heap,
                           std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_d3d12RtvDescriptorCapacity> a_handles,
                           std::uint32_t a_incrementSize, std::uint64_t a_incarnation,
                           const AssertContext &a_assertContext) noexcept
    : m_heap(std::move(a_heap)), m_handles(a_handles), m_generations{}, m_allocated{},
      m_assertContext(&a_assertContext), m_incrementSize(a_incrementSize), m_usedCount(0), m_incarnation(a_incarnation)
{
}

D3d12RtvHeap::D3d12RtvHeap(D3d12RtvHeap &&a_other) noexcept
    : m_handles{}, m_generations{}, m_allocated{}, m_assertContext(nullptr), m_incrementSize(0), m_usedCount(0),
      m_incarnation(0)
{
    take_from(std::move(a_other));
}

D3d12RtvHeap &D3d12RtvHeap::operator=(D3d12RtvHeap &&a_other) noexcept
{
    if (this != &a_other)
    {
        if (has_native_object())
        {
            m_assertContext->fatal_handler().terminate("D3D12 RTV Heap move assignment requires an empty target");
        }

        take_from(std::move(a_other));
    }

    return *this;
}

D3d12RtvHeap::~D3d12RtvHeap() noexcept
{
    if (has_native_object())
    {
        m_assertContext->fatal_handler().terminate("D3D12 RTV Heap owner was destroyed before shutdown");
    }
}

void D3d12RtvHeap::take_from(D3d12RtvHeap &&a_other) noexcept
{
    m_heap = std::move(a_other.m_heap);
    m_handles = a_other.m_handles;
    m_generations = a_other.m_generations;
    m_allocated = a_other.m_allocated;
    m_assertContext = a_other.m_assertContext;
    m_incrementSize = a_other.m_incrementSize;
    m_usedCount = a_other.m_usedCount;
    m_incarnation = a_other.m_incarnation;

    a_other.m_handles = {};
    a_other.m_generations = {};
    a_other.m_allocated = {};
    a_other.m_incrementSize = 0;
    a_other.m_usedCount = 0;
    a_other.m_incarnation = 0;
}

// Slot 再利用時に Generation を進め、解放前に取得した古い Slot を同じ Index で誤認しないようにする
Result<D3d12RtvSlot> D3d12RtvHeap::allocate() noexcept
{
    if (!has_native_object())
    {
        return Result<D3d12RtvSlot>::failure(
            make_error(*m_assertContext, k_heapShutdown, "D3D12 RTV Heap is shutdown"));
    }

    for (std::uint32_t index = 0; index < k_d3d12RtvDescriptorCapacity; ++index)
    {
        if (!m_allocated[index])
        {
            if (m_generations[index] == (std::numeric_limits<std::uint64_t>::max)())
            {
                continue;
            }

            ++m_generations[index];
            m_allocated[index] = true;
            ++m_usedCount;
            D3d12RtvSlot slot = {m_incarnation, index, m_generations[index]};
            return Result<D3d12RtvSlot>::success(std::move(slot));
        }
    }

    if (m_usedCount < k_d3d12RtvDescriptorCapacity)
    {
        return Result<D3d12RtvSlot>::failure(make_error(*m_assertContext, k_slotGenerationExhausted,
                                                        "D3D12 RTV Descriptor slot generation is exhausted"));
    }

    return Result<D3d12RtvSlot>::failure(
        make_error(*m_assertContext, k_capacityExhausted, "D3D12 RTV Descriptor capacity is exhausted"));
}

bool D3d12RtvHeap::is_live_slot(D3d12RtvSlot a_slot) const noexcept
{
    return a_slot.heapIncarnation == m_incarnation && a_slot.index < k_d3d12RtvDescriptorCapacity &&
           m_allocated[a_slot.index] && m_generations[a_slot.index] == a_slot.generation;
}

Result<void> D3d12RtvHeap::release(D3d12RtvSlot a_slot) noexcept
{
    if (a_slot.index >= k_d3d12RtvDescriptorCapacity)
    {
        CUE_ASSERT(*m_assertContext, false, "D3D12 RTV Descriptor slot index is out of range");
        return Result<void>::failure(
            make_error(*m_assertContext, k_invalidSlotIndex, "D3D12 RTV Descriptor slot index is out of range"));
    }

    if (!is_live_slot(a_slot))
    {
        CUE_ASSERT(*m_assertContext, false, "D3D12 RTV Descriptor slot is inactive or stale");
        return Result<void>::failure(
            make_error(*m_assertContext, k_inactiveSlot, "D3D12 RTV Descriptor slot is inactive or stale"));
    }

    m_allocated[a_slot.index] = false;
    --m_usedCount;
    return Result<void>::success();
}

// 生成時に事前計算した CPU Handle を Slot の世代と所有権を検証してから返す
Result<D3D12_CPU_DESCRIPTOR_HANDLE> D3d12RtvHeap::cpu_handle(D3d12RtvSlot a_slot) const noexcept
{
    if (a_slot.index >= k_d3d12RtvDescriptorCapacity)
    {
        CUE_ASSERT(*m_assertContext, false, "D3D12 RTV Descriptor handle index is out of range");
        return Result<D3D12_CPU_DESCRIPTOR_HANDLE>::failure(
            make_error(*m_assertContext, k_invalidSlotIndex, "D3D12 RTV Descriptor handle index is out of range"));
    }

    if (!is_live_slot(a_slot))
    {
        CUE_ASSERT(*m_assertContext, false, "D3D12 RTV Descriptor handle requires a live slot");
        return Result<D3D12_CPU_DESCRIPTOR_HANDLE>::failure(
            make_error(*m_assertContext, k_inactiveSlot, "D3D12 RTV Descriptor handle requires a live slot"));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_handles[a_slot.index];
    return Result<D3D12_CPU_DESCRIPTOR_HANDLE>::success(std::move(handle));
}

// 使用中 Slot が残る間は Heap 解放を拒否し、Command 記録が参照する Descriptor の消失を防ぐ
Result<void> D3d12RtvHeap::shutdown() noexcept
{
    if (!has_native_object())
    {
        return Result<void>::success();
    }

    if (m_usedCount != 0)
    {
        CUE_ASSERT(*m_assertContext, false, "D3D12 RTV Heap shutdown requires all slots to be released");
        return Result<void>::failure(
            make_error(*m_assertContext, k_liveSlotsAtShutdown, "D3D12 RTV Heap has live Descriptor slots"));
    }

    m_heap.Reset();
    m_handles = {};
    m_generations = {};
    m_allocated = {};
    m_incrementSize = 0;
    m_incarnation = 0;
    return Result<void>::success();
}

std::uint32_t D3d12RtvHeap::capacity() const noexcept
{
    return k_d3d12RtvDescriptorCapacity;
}

std::uint32_t D3d12RtvHeap::used_count() const noexcept
{
    return m_usedCount;
}

std::uint32_t D3d12RtvHeap::descriptor_increment_size() const noexcept
{
    return m_incrementSize;
}

bool D3d12RtvHeap::has_native_object() const noexcept
{
    return m_heap != nullptr;
}

void D3d12RtvHeap::set_slot_generation_for_test(std::uint32_t a_slotIndex, std::uint64_t a_generation) noexcept
{
    CUE_ASSERT(*m_assertContext, a_slotIndex < k_d3d12RtvDescriptorCapacity,
               "D3D12 RTV Descriptor test generation index is out of range");
    CUE_ASSERT(*m_assertContext, a_slotIndex < k_d3d12RtvDescriptorCapacity && !m_allocated[a_slotIndex],
               "D3D12 RTV Descriptor test generation requires a free slot");

    if (a_slotIndex < k_d3d12RtvDescriptorCapacity && !m_allocated[a_slotIndex])
    {
        m_generations[a_slotIndex] = a_generation;
    }
}

Result<D3d12RtvHeap> create_d3d12_rtv_heap(ID3D12Device *a_device, const AssertContext &a_assertContext,
                                           const D3d12RtvHeapNativeFunctions &a_functions,
                                           const D3d12RtvHeapFailureHandler &a_failureHandler) noexcept
{
    if (a_device == nullptr)
    {
        return Result<D3d12RtvHeap>::failure(
            make_error(a_assertContext, k_invalidDevice, "D3D12 Device is required for RTV Heap creation"));
    }

    D3D12_DESCRIPTOR_HEAP_DESC descriptor = {};
    descriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptor.NumDescriptors = k_d3d12RtvDescriptorCapacity;
    descriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    descriptor.NodeMask = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT creationResult = a_functions.createDescriptorHeap(a_device, &descriptor, heap.GetAddressOf());

    if (FAILED(creationResult))
    {
        return fail_native_creation(make_native_error(a_assertContext, k_heapCreationFailed,
                                                      "D3D12 RTV Descriptor Heap creation failed", creationResult),
                                    a_failureHandler, heap.Get(), a_assertContext);
    }

    const HRESULT nameResult = a_functions.setObjectName(heap.Get(), L"CueEngine D3D12 Presentation RTV Heap");

    if (FAILED(nameResult))
    {
        return fail_native_creation(make_native_error(a_assertContext, k_heapNameFailed,
                                                      "D3D12 RTV Descriptor Heap diagnostic name could not be set",
                                                      nameResult),
                                    a_failureHandler, heap.Get(), a_assertContext);
    }

    const std::uint32_t incrementSize = a_functions.getDescriptorHandleIncrementSize(a_device);

    if (incrementSize == 0)
    {
        return Result<D3d12RtvHeap>::failure(make_error(a_assertContext, k_invalidIncrementSize,
                                                        "D3D12 RTV Descriptor increment size must be non-zero"));
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE startHandle = a_functions.getCpuDescriptorHandleForHeapStart(heap.Get());

    if (startHandle.ptr == 0)
    {
        return Result<D3d12RtvHeap>::failure(make_error(a_assertContext, k_invalidIncrementSize,
                                                        "D3D12 RTV Descriptor CPU handle start must be non-zero"));
    }

    if (startHandle.ptr > (std::numeric_limits<SIZE_T>::max)() - incrementSize)
    {
        return Result<D3d12RtvHeap>::failure(
            make_error(a_assertContext, k_handleOverflow, "D3D12 RTV Descriptor CPU handle overflowed"));
    }

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_d3d12RtvDescriptorCapacity> handles = {
        startHandle,
        {startHandle.ptr + incrementSize},
    };
    const std::uint64_t incarnation = reserve_heap_incarnation();

    if (incarnation == 0)
    {
        return Result<D3d12RtvHeap>::failure(
            make_error(a_assertContext, k_heapIncarnationExhausted, "D3D12 RTV Heap incarnation range is exhausted"));
    }

    D3d12RtvHeap owner(std::move(heap), handles, incrementSize, incarnation, a_assertContext);
    return Result<D3d12RtvHeap>::success(std::move(owner));
}
} // namespace cue
