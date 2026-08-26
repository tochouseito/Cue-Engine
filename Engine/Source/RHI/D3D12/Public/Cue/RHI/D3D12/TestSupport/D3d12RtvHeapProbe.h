#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>

namespace cue
{
class AssertContext;

struct D3d12RtvHeapProbeReport final
{
    std::uint32_t capacity;
    std::uint32_t usedAtCapacity;
    std::uint32_t firstSlotIndex;
    std::uint32_t secondSlotIndex;
    std::uint32_t reusedSlotIndex;
    std::uint32_t descriptorIncrementSize;
    std::uint64_t infoQueueErrorCount;
    bool descriptorShapeIsValid;
    bool cpuHandlesAreValid;
    bool capacityErrorDetected;
    bool generationAdvancedOnReuse;
    bool diagnosticsAvailable;
};

enum class D3d12RtvHeapViolationProbeMode
{
    DoubleRelease,
    StaleRelease,
    ForeignRelease,
    LiveSlotShutdown,
};

enum class D3d12RtvHeapOwnerViolationProbeMode
{
    DestructorBeforeShutdown,
    ActiveMoveAssignment,
};

/// @brief WARP Device上で2 slot RTV Heapの割当、解放、再利用、診断を検証する
[[nodiscard]] Result<D3d12RtvHeapProbeReport> probe_d3d12_rtv_heap(const AssertContext &a_assertContext) noexcept;

/// @brief 生成失敗、命名失敗、無効incrementのrollbackを検証する
[[nodiscard]] bool verify_d3d12_rtv_heap_creation_faults_for_probe(const AssertContext &a_assertContext) noexcept;

/// @brief CPU Descriptor handle加算overflowをErrorとして検出することを検証する
[[nodiscard]] bool verify_d3d12_rtv_heap_handle_overflow_for_probe(const AssertContext &a_assertContext) noexcept;

/// @brief Slot世代がwrapせず明示的に枯渇することを検証する
[[nodiscard]] bool verify_d3d12_rtv_heap_generation_exhaustion_for_probe(const AssertContext &a_assertContext) noexcept;

/// @brief Heap move後も既存slot tokenのHeap identityが維持されることを検証する
[[nodiscard]] bool verify_d3d12_rtv_heap_move_for_probe(const AssertContext &a_assertContext) noexcept;

/// @brief 所有権違反をDebug AssertまたはRelease Errorとして検証する
[[nodiscard]] bool verify_d3d12_rtv_heap_violation_for_probe(D3d12RtvHeapViolationProbeMode a_mode,
                                                             const AssertContext &a_assertContext) noexcept;

/// @brief Native ownerを暗黙破棄または上書きした場合にFatal終了することを検証する
[[nodiscard]] bool trigger_d3d12_rtv_heap_owner_violation_for_probe(D3d12RtvHeapOwnerViolationProbeMode a_mode,
                                                                    const AssertContext &a_assertContext) noexcept;
} // namespace cue
