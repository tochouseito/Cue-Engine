#pragma once

#include <Cue/Foundation/Capability.h>
#include <Cue/Foundation/Result.h>

#include <cstdint>
#include <optional>
#include <string>

namespace cue
{
/// @brief Native Graphics API 型を公開せずに Backend 実装を識別する値
enum class GraphicsBackendKind
{
    D3d12,
};

/// @brief Adapter 選択結果を Hardware と Software の役割で分類する値
enum class GraphicsAdapterKind
{
    Hardware,
    Software,
};

/// @brief Renderer が利用可能な最小 Graphics 機能契約を識別する Profile
enum class GraphicsProfile
{
    Baseline3D,
};

/// @brief Native定数へ依存しないD3D12世代のGraphics Feature Level
enum class GraphicsFeatureLevel
{
    Level12_0,
    Level12_1,
    Level12_2,
};

/// @brief Resource Descriptor BindingのHardware Tier
enum class ResourceBindingTier
{
    Tier1,
    Tier2,
    Tier3,
};

/// @brief Resource Heap制約のHardware Tier
enum class ResourceHeapTier
{
    Tier1,
    Tier2,
};

/// @brief Hardware Ray Tracing機能のTier
enum class RayTracingTier
{
    Tier1_0,
    Tier1_1,
    Tier1_2,
};

/// @brief Mesh Shader機能のTier
enum class MeshShaderTier
{
    Tier1,
};

/// @brief Variable Rate Shading機能のTier
enum class VariableRateShadingTier
{
    Tier1,
    Tier2,
};

/// @brief Sampler Feedback機能のTier
enum class SamplerFeedbackTier
{
    Tier0_9,
    Tier1_0,
};

/// @brief Query状態、Support状態、対応時の型付き値を一つに束ねるGraphics Capability値
template <typename Value> class GraphicsCapabilityValue final
{
  public:
    /// @brief Query対象外または未実行により値が不明な状態を返す
    [[nodiscard]] static constexpr GraphicsCapabilityValue not_queried() noexcept
    {
        return GraphicsCapabilityValue(CapabilitySupportState::not_queried(), std::nullopt);
    }

    /// @brief Query失敗により値が不明な状態を返す
    [[nodiscard]] static constexpr GraphicsCapabilityValue query_failed() noexcept
    {
        return GraphicsCapabilityValue(CapabilitySupportState::query_failed(), std::nullopt);
    }

    /// @brief Query成功によりHardware未対応と判明した状態を返す
    [[nodiscard]] static constexpr GraphicsCapabilityValue unsupported() noexcept
    {
        return GraphicsCapabilityValue(CapabilitySupportState::unsupported(), std::nullopt);
    }

    /// @brief Query成功で取得したHardware対応値を保持する状態を返す
    [[nodiscard]] static constexpr GraphicsCapabilityValue supported(Value a_value) noexcept
    {
        return GraphicsCapabilityValue(CapabilitySupportState::supported(), a_value);
    }

    /// @brief Query結果とHardware対応状態を返す
    [[nodiscard]] constexpr CapabilitySupportState support_state() const noexcept
    {
        return m_supportState;
    }

    /// @brief Hardware対応値がある場合だけ非所有Pointerを返す
    [[nodiscard]] constexpr const Value *try_value() const noexcept
    {
        return m_value ? &*m_value : nullptr;
    }

  private:
    /// @brief 名前付きFactoryが保証したSupport状態と任意値を保持する
    constexpr GraphicsCapabilityValue(CapabilitySupportState a_supportState, std::optional<Value> a_value) noexcept
        : m_value(a_value), m_supportState(a_supportState)
    {
    }

    std::optional<Value> m_value;
    CapabilitySupportState m_supportState;
};

static_assert(GraphicsCapabilityValue<GraphicsFeatureLevel>::supported(GraphicsFeatureLevel::Level12_0)
                  .support_state()
                  .support() == CapabilitySupport::Supported);
static_assert(GraphicsCapabilityValue<ResourceBindingTier>::unsupported().try_value() == nullptr);
static_assert(GraphicsCapabilityValue<ResourceHeapTier>::query_failed().support_state().query_status() ==
              CapabilityQueryStatus::Failed);

/// @brief Native Resource の保持段階と Owner の破棄可否を表す Graphics Backend の Lifecycle 状態
enum class GraphicsBackendState
{
    /// @brief Native Resource は利用可能だが、Context の有無などにより個別操作が拒否され得る状態
    Ready,

    /// @brief Device Removal 後に診断と制御された解放だけを許可する状態
    DeviceRemoved,

    /// @brief 安全な解放を証明できず、Process 終了まで Owner を保持する状態
    Unavailable,

    /// @brief Native Resource の解放が完了し、Owner を破棄できる状態
    Shutdown,
};

/// @brief Feature 選択と診断に使用する Platform 非依存の Graphics Capability Snapshot
struct CapabilityReport final
{
    /// @brief UTF-8 の Adapter 表示名
    std::string adapterName;

    /// @brief Dedicated Video Memory の Byte 数
    std::uint64_t dedicatedVideoMemoryBytes;

    /// @brief PCI Vendor ID
    std::uint32_t vendorId;

    /// @brief PCI Device ID
    std::uint32_t deviceId;

    /// @brief 選択された Backend の実装種別
    GraphicsBackendKind backendKind;

    /// @brief 選択された Adapter の種別
    GraphicsAdapterKind adapterKind;

    /// @brief 提供される Graphics Profile
    GraphicsProfile profile;

    /// @brief Baselineを含む最大Feature LevelのQuery結果
    GraphicsCapabilityValue<GraphicsFeatureLevel> featureLevel =
        GraphicsCapabilityValue<GraphicsFeatureLevel>::not_queried();

    /// @brief 最大Shader Model VersionのQuery結果
    GraphicsCapabilityValue<CapabilityVersion> shaderModel = GraphicsCapabilityValue<CapabilityVersion>::not_queried();

    /// @brief 最大Root Signature VersionのQuery結果
    GraphicsCapabilityValue<CapabilityVersion> rootSignature =
        GraphicsCapabilityValue<CapabilityVersion>::not_queried();

    /// @brief Resource Binding TierのQuery結果
    GraphicsCapabilityValue<ResourceBindingTier> resourceBinding =
        GraphicsCapabilityValue<ResourceBindingTier>::not_queried();

    /// @brief Resource Heap TierのQuery結果
    GraphicsCapabilityValue<ResourceHeapTier> resourceHeap = GraphicsCapabilityValue<ResourceHeapTier>::not_queried();

    /// @brief Ray Tracing TierのQuery結果
    GraphicsCapabilityValue<RayTracingTier> rayTracing = GraphicsCapabilityValue<RayTracingTier>::not_queried();

    /// @brief Mesh Shader TierのQuery結果
    GraphicsCapabilityValue<MeshShaderTier> meshShader = GraphicsCapabilityValue<MeshShaderTier>::not_queried();

    /// @brief Variable Rate Shading TierのQuery結果
    GraphicsCapabilityValue<VariableRateShadingTier> variableRateShading =
        GraphicsCapabilityValue<VariableRateShadingTier>::not_queried();

    /// @brief Sampler Feedback TierのQuery結果
    GraphicsCapabilityValue<SamplerFeedbackTier> samplerFeedback =
        GraphicsCapabilityValue<SamplerFeedbackTier>::not_queried();

    /// @brief Wave Operation対応のQuery結果
    CapabilitySupportState waveOperations = CapabilitySupportState::not_queried();

    /// @brief Enhanced Barrier対応のQuery結果
    CapabilitySupportState enhancedBarriers = CapabilitySupportState::not_queried();

    /// @brief Unified Memory Architecture対応のQuery結果
    CapabilitySupportState uma = CapabilitySupportState::not_queried();

    /// @brief Cache Coherent UMA対応のQuery結果
    CapabilitySupportState cacheCoherentUma = CapabilitySupportState::not_queried();
};

/// @brief Platform 非依存の Graphics Backend 所有契約
///
/// Backend は生成 Thread 上で一意所有し、全ての公開操作と Destructor を同じ Thread 上で呼び出す
///
/// `Unavailable` は安全な Resource 解放を証明できない Process 終端状態であり、Owner を破棄しない
class GraphicsBackend
{
  public:
    /// @brief 明示 Shutdown 後に Backend Owner を破棄する
    ///
    /// `Shutdown`、または GPU Work を投入していない初期化失敗状態だけで破棄できる
    virtual ~GraphicsBackend() noexcept;

    /// @brief GraphicsBackend の一意所有を保つため Copy 構築を禁止する
    GraphicsBackend(const GraphicsBackend &) = delete;
    /// @brief GraphicsBackend の一意所有を保つため Copy 代入を禁止する
    GraphicsBackend &operator=(const GraphicsBackend &) = delete;

    /// @brief Backend が保持する Capability Report を返す
    /// @return Backend 破棄開始まで有効な Immutable 参照
    [[nodiscard]] virtual const CapabilityReport &capabilities() const noexcept = 0;

    /// @brief 現在の Backend Lifecycle 状態を返す
    [[nodiscard]] virtual GraphicsBackendState state() const noexcept = 0;

    /// @brief Backend を安全に停止する
    /// @return 停止成功、または診断可能な停止 Error
    ///
    /// `Shutdown` では成功する冪等操作とする
    /// `Unavailable` では Resource を解放せず `RHI.BackendUnavailable` を返す
    /// 有効な Presentation Context が残る場合は Native Resource を解放せず `RHI.ActivePresentationContexts` を返す
    /// `DeviceRemoved` では利用可能な Backend 固有診断を適切な解放時点で試行し、
    /// 安全に解放できた Native Resource を解放する
    /// 診断または解放の Error を返す場合がある
    [[nodiscard]] virtual Result<void> shutdown() noexcept = 0;

  protected:
    /// @brief GraphicsBackend を必要な依存と初期状態から構築する
    GraphicsBackend() = default;
};
} // namespace cue
