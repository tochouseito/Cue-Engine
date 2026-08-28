#pragma once

#include <compare>
#include <cstdint>

namespace cue
{
/// @brief Hardware Capability Query の実行状態
enum class CapabilityQueryStatus
{
    NotQueried,
    Succeeded,
    Failed,
};

/// @brief Query で確認した Hardware Capability の対応状態
enum class CapabilitySupport
{
    Unknown,
    Unsupported,
    Supported,
};

/// @brief CueEngine が Capability の Production 経路を持つかを表す状態
enum class CapabilityImplementation
{
    NotImplemented,
    Implemented,
};

/// @brief Runtime Policy が実装済み Capability を使用するかを表す状態
enum class CapabilityEnablement
{
    NotApplicable,
    Disabled,
    Enabled,
};

/// @brief Query 結果と Hardware Support の有効な組合せだけを表現する値型
class CapabilitySupportState final
{
  public:
    /// @brief Query 対象外または未実行により対応状況が不明な状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState not_queried() noexcept
    {
        return CapabilitySupportState(CapabilityQueryStatus::NotQueried, CapabilitySupport::Unknown);
    }

    /// @brief Query 失敗により対応状況が不明な状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState query_failed() noexcept
    {
        return CapabilitySupportState(CapabilityQueryStatus::Failed, CapabilitySupport::Unknown);
    }

    /// @brief Query 成功により Hardware 対応済みと判明した状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState supported() noexcept
    {
        return CapabilitySupportState(CapabilityQueryStatus::Succeeded, CapabilitySupport::Supported);
    }

    /// @brief Query 成功により Hardware 未対応と判明した状態を返す
    [[nodiscard]] static constexpr CapabilitySupportState unsupported() noexcept
    {
        return CapabilitySupportState(CapabilityQueryStatus::Succeeded, CapabilitySupport::Unsupported);
    }

    /// @brief Capability Query の実行状態を返す
    [[nodiscard]] constexpr CapabilityQueryStatus query_status() const noexcept
    {
        return m_queryStatus;
    }

    /// @brief Hardware Capability の対応状態を返す
    [[nodiscard]] constexpr CapabilitySupport support() const noexcept
    {
        return m_support;
    }

  private:
    /// @brief 名前付き Factory が保証した有効な Query と Support の組合せを保持する
    constexpr CapabilitySupportState(CapabilityQueryStatus a_queryStatus, CapabilitySupport a_support) noexcept
        : m_queryStatus(a_queryStatus), m_support(a_support)
    {
    }

    CapabilityQueryStatus m_queryStatus;
    CapabilitySupport m_support;
};

/// @brief Hardware、Engine実装、Runtime有効化の有効な組合せだけを表現する値型
class CapabilityState final
{
  public:
    /// @brief 未 Query かつ Engine 未実装で有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState not_queried_not_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::not_queried(), CapabilityImplementation::NotImplemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief 未 Query かつ Engine 実装済みで有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState not_queried_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::not_queried(), CapabilityImplementation::Implemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief Query 失敗かつ Engine 未実装で有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState query_failed_not_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::query_failed(), CapabilityImplementation::NotImplemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief Query 失敗かつ Engine 実装済みで有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState query_failed_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::query_failed(), CapabilityImplementation::Implemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief Hardware 未対応かつ Engine 未実装で有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState unsupported_not_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::unsupported(), CapabilityImplementation::NotImplemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief Hardware 未対応かつ Engine 実装済みで有効化対象外の状態を返す
    [[nodiscard]] static constexpr CapabilityState unsupported_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::unsupported(), CapabilityImplementation::Implemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief Hardware 対応済みだが Engine 未実装の状態を返す
    [[nodiscard]] static constexpr CapabilityState supported_not_implemented() noexcept
    {
        return CapabilityState(CapabilitySupportState::supported(), CapabilityImplementation::NotImplemented,
                               CapabilityEnablement::NotApplicable);
    }

    /// @brief Hardware と Engine は対応済みだが Runtime Policy で無効な状態を返す
    [[nodiscard]] static constexpr CapabilityState supported_disabled() noexcept
    {
        return CapabilityState(CapabilitySupportState::supported(), CapabilityImplementation::Implemented,
                               CapabilityEnablement::Disabled);
    }

    /// @brief Hardware と Engine が対応し Runtime Policy でも有効な状態を返す
    [[nodiscard]] static constexpr CapabilityState supported_enabled() noexcept
    {
        return CapabilityState(CapabilitySupportState::supported(), CapabilityImplementation::Implemented,
                               CapabilityEnablement::Enabled);
    }

    /// @brief Hardware Query と Support の組合せを返す
    [[nodiscard]] constexpr CapabilitySupportState hardware() const noexcept
    {
        return m_hardware;
    }

    /// @brief CueEngine の Production 実装状態を返す
    [[nodiscard]] constexpr CapabilityImplementation implementation() const noexcept
    {
        return m_implementation;
    }

    /// @brief 現在の Runtime Policy による有効状態を返す
    [[nodiscard]] constexpr CapabilityEnablement enablement() const noexcept
    {
        return m_enablement;
    }

  private:
    /// @brief 名前付き Factory が保証した有効な3状態を保持する
    constexpr CapabilityState(CapabilitySupportState a_hardware, CapabilityImplementation a_implementation,
                              CapabilityEnablement a_enablement) noexcept
        : m_hardware(a_hardware), m_implementation(a_implementation), m_enablement(a_enablement)
    {
    }

    CapabilitySupportState m_hardware;
    CapabilityImplementation m_implementation;
    CapabilityEnablement m_enablement;
};

/// @brief Native 定数へ依存せず Capability Version を比較する値型
struct CapabilityVersion final
{
    std::uint16_t major;
    std::uint16_t minor;

    /// @brief Major、Minor の辞書順で Version を比較する
    [[nodiscard]] constexpr auto operator<=>(const CapabilityVersion &) const noexcept = default;
};

static_assert(CapabilitySupportState::not_queried().query_status() == CapabilityQueryStatus::NotQueried);
static_assert(CapabilitySupportState::not_queried().support() == CapabilitySupport::Unknown);
static_assert(CapabilitySupportState::query_failed().query_status() == CapabilityQueryStatus::Failed);
static_assert(CapabilitySupportState::query_failed().support() == CapabilitySupport::Unknown);
static_assert(CapabilitySupportState::supported().support() == CapabilitySupport::Supported);
static_assert(CapabilitySupportState::unsupported().support() == CapabilitySupport::Unsupported);
static_assert(CapabilityState::supported_enabled().enablement() == CapabilityEnablement::Enabled);
static_assert(CapabilityState::supported_disabled().enablement() == CapabilityEnablement::Disabled);
static_assert(CapabilityState::query_failed_implemented().hardware().query_status() == CapabilityQueryStatus::Failed);
} // namespace cue
