#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>
#include <string>

namespace cue
{
/** @brief Native Graphics API 型を公開せずに Backend 実装を識別する値 */
enum class GraphicsBackendKind
{
    D3d12,
};

/** @brief Adapter 選択結果を Hardware と Software の役割で分類する値 */
enum class GraphicsAdapterKind
{
    Hardware,
    Software,
};

/** @brief Renderer が利用可能な最小 Graphics 機能契約を識別する Profile */
enum class GraphicsProfile
{
    Baseline3D,
};

/** @brief Native Resource の保持段階と Owner の破棄可否を表す Graphics Backend の Lifecycle 状態 */
enum class GraphicsBackendState
{
    /** @brief Native Resource は利用可能だが、Context の有無などにより個別操作が拒否され得る状態 */
    Ready,

    /** @brief Device Removal 後に診断と制御された解放だけを許可する状態 */
    DeviceRemoved,

    /** @brief 安全な解放を証明できず、Process 終了まで Owner を保持する状態 */
    Unavailable,

    /** @brief Native Resource の解放が完了し、Owner を破棄できる状態 */
    Shutdown,
};

/** @brief Feature 選択と診断に使用する Platform 非依存の Graphics Capability Snapshot */
struct CapabilityReport final
{
    /** @brief UTF-8 の Adapter 表示名 */
    std::string adapterName;

    /** @brief Dedicated Video Memory の Byte 数 */
    std::uint64_t dedicatedVideoMemoryBytes;

    /** @brief PCI Vendor ID */
    std::uint32_t vendorId;

    /** @brief PCI Device ID */
    std::uint32_t deviceId;

    /** @brief 選択された Backend の実装種別 */
    GraphicsBackendKind backendKind;

    /** @brief 選択された Adapter の種別 */
    GraphicsAdapterKind adapterKind;

    /** @brief 提供される Graphics Profile */
    GraphicsProfile profile;

    /** @brief Unified Memory Architecture の場合は true */
    bool isUma;
};

/**
 * @brief Platform 非依存の Graphics Backend 所有契約
 *
 * Backend は生成 Thread 上で一意所有し、全ての公開操作と Destructor を同じ Thread 上で呼び出す
 *
 * `Unavailable` は安全な Resource 解放を証明できない Process 終端状態であり、Owner を破棄しない
 */
class GraphicsBackend
{
  public:
    /**
     * @brief 明示 Shutdown 後に Backend Owner を破棄する
     *
     * `Shutdown`、または GPU Work を投入していない初期化失敗状態だけで破棄できる
     */
    virtual ~GraphicsBackend() noexcept;

    GraphicsBackend(const GraphicsBackend &) = delete;
    GraphicsBackend &operator=(const GraphicsBackend &) = delete;

    /**
     * @brief Backend が保持する Capability Report を返す
     * @return Backend 破棄開始まで有効な Immutable 参照
     */
    [[nodiscard]] virtual const CapabilityReport &capabilities() const noexcept = 0;

    /** @brief 現在の Backend Lifecycle 状態を返す */
    [[nodiscard]] virtual GraphicsBackendState state() const noexcept = 0;

    /**
     * @brief Backend を安全に停止する
     * @return 停止成功、または診断可能な停止 Error
     *
     * `Shutdown` では成功する冪等操作とする
     * `Unavailable` では Resource を解放せず `RHI.BackendUnavailable` を返す
     * 有効な Presentation Context が残る場合は Native Resource を解放せず `RHI.ActivePresentationContexts` を返す
     * `DeviceRemoved` では利用可能な Backend 固有診断を適切な解放時点で試行し、
     * 安全に解放できた Native Resource を解放する
     * 診断または解放の Error を返す場合がある
     */
    [[nodiscard]] virtual Result<void> shutdown() noexcept = 0;

  protected:
    GraphicsBackend() = default;
};
} // namespace cue
