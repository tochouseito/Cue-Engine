#pragma once

#include <Cue/Foundation/Capability.h>
#include <Cue/Foundation/Log.h>

#include <cstdint>
#include <optional>

namespace cue
{
/// @brief Process または Native Machine の Platform 非依存 Architecture
enum class SystemArchitecture
{
    Unknown,
    X64,
    Arm64,
};

/// @brief 数値 Query の未実行、失敗、成功値を区別する値型
template <typename Value>
class SystemCapabilityValue final
{
  public:
    /// @brief Query 対象外または未実行で値が不明な状態を返す
    [[nodiscard]] static constexpr SystemCapabilityValue not_queried() noexcept
    {
        return SystemCapabilityValue(CapabilityQueryStatus::NotQueried, std::nullopt);
    }

    /// @brief Query 失敗により値が不明な状態を返す
    [[nodiscard]] static constexpr SystemCapabilityValue query_failed() noexcept
    {
        return SystemCapabilityValue(CapabilityQueryStatus::Failed, std::nullopt);
    }

    /// @brief Query 成功で取得した値を保持する状態を返す
    [[nodiscard]] static constexpr SystemCapabilityValue known(Value a_value) noexcept
    {
        return SystemCapabilityValue(CapabilityQueryStatus::Succeeded, a_value);
    }

    /// @brief 数値 Query の実行状態を返す
    [[nodiscard]] constexpr CapabilityQueryStatus query_status() const noexcept
    {
        return m_queryStatus;
    }

    /// @brief Query 成功値がある場合だけ非所有 Pointer を返す
    [[nodiscard]] constexpr const Value *try_value() const noexcept
    {
        return m_value ? &*m_value : nullptr;
    }

  private:
    /// @brief 名前付き Factory が保証した Query 状態と任意値を保持する
    constexpr SystemCapabilityValue(CapabilityQueryStatus a_queryStatus, std::optional<Value> a_value) noexcept
        : m_value(a_value), m_queryStatus(a_queryStatus)
    {
    }

    std::optional<Value> m_value;
    CapabilityQueryStatus m_queryStatus;
};

/// @brief CPU InstructionとOS Context SaveのSupport状態をまとめる値
struct CpuInstructionCapabilities final
{
    CapabilitySupportState sse2;
    CapabilitySupportState sse3;
    CapabilitySupportState ssse3;
    CapabilitySupportState sse41;
    CapabilitySupportState sse42;
    CapabilitySupportState avx;
    CapabilitySupportState avx2;
    CapabilitySupportState fma;
    CapabilitySupportState osExtendedState;
};

/// @brief System Capability Snapshot生成に必要なPlatform非依存値
struct SystemCapabilitySnapshotDescription final
{
    SystemArchitecture processArchitecture;
    SystemArchitecture nativeArchitecture;
    SystemCapabilityValue<std::uint32_t> logicalProcessorCount;
    SystemCapabilityValue<std::uint64_t> physicalMemoryBytes;
    SystemCapabilityValue<std::uint32_t> pageSizeBytes;
    SystemCapabilityValue<std::uint32_t> cacheLineSizeBytes;
    CpuInstructionCapabilities instructions;
};

/// @brief Query完了後に変更されず、Native型や非所有参照を持たないSystem Capability値
class SystemCapabilitySnapshot final
{
  public:
    /// @brief 完成済みDescriptionを不変Snapshotへ変換する
    [[nodiscard]] static constexpr SystemCapabilitySnapshot create(
        SystemCapabilitySnapshotDescription a_description) noexcept
    {
        return SystemCapabilitySnapshot(a_description);
    }

    /// @brief Process Architectureを返す
    [[nodiscard]] constexpr SystemArchitecture process_architecture() const noexcept
    {
        return m_description.processArchitecture;
    }

    /// @brief Native Machine Architectureを返す
    [[nodiscard]] constexpr SystemArchitecture native_architecture() const noexcept
    {
        return m_description.nativeArchitecture;
    }

    /// @brief Logical Processor数のQuery結果を返す
    [[nodiscard]] constexpr const SystemCapabilityValue<std::uint32_t> &logical_processor_count() const noexcept
    {
        return m_description.logicalProcessorCount;
    }

    /// @brief Physical Memory Byte数のQuery結果を返す
    [[nodiscard]] constexpr const SystemCapabilityValue<std::uint64_t> &physical_memory_bytes() const noexcept
    {
        return m_description.physicalMemoryBytes;
    }

    /// @brief Memory Page SizeのQuery結果を返す
    [[nodiscard]] constexpr const SystemCapabilityValue<std::uint32_t> &page_size_bytes() const noexcept
    {
        return m_description.pageSizeBytes;
    }

    /// @brief CPU Cache Line SizeのQuery結果を返す
    [[nodiscard]] constexpr const SystemCapabilityValue<std::uint32_t> &cache_line_size_bytes() const noexcept
    {
        return m_description.cacheLineSizeBytes;
    }

    /// @brief CPU InstructionとOS Context SaveのSupport状態を返す
    [[nodiscard]] constexpr const CpuInstructionCapabilities &instructions() const noexcept
    {
        return m_description.instructions;
    }

  private:
    /// @brief 検証済みDescriptionをSnapshotの所有値として保持する
    explicit constexpr SystemCapabilitySnapshot(SystemCapabilitySnapshotDescription a_description) noexcept
        : m_description(a_description)
    {
    }

    SystemCapabilitySnapshotDescription m_description;
};

/// @brief 完成済みSnapshotとNative失敗診断の配送結果を返す値
struct SystemCapabilityQueryReport final
{
    SystemCapabilitySnapshot snapshot;
    LogResult diagnosticResult;
};
} // namespace cue
