#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>
#include <string>

namespace cue
{
/** @brief Graphics Backendの実装種別 */
enum class GraphicsBackendKind
{
    D3d12,
};

/** @brief 選択されたGraphics Adapterの種別 */
enum class GraphicsAdapterKind
{
    Hardware,
    Software,
};

/** @brief Graphics機能契約のProfile */
enum class GraphicsProfile
{
    Baseline3D,
};

/** @brief Graphics BackendのLifecycle状態 */
enum class GraphicsBackendState
{
    Ready,
    DeviceRemoved,
    Unavailable,
    Shutdown,
};

/** @brief Platform非依存のGraphics Capability値 */
struct CapabilityReport final
{
    /** @brief UTF-8のAdapter表示名 */
    std::string adapterName;

    /** @brief Dedicated Video MemoryのByte数 */
    std::uint64_t dedicatedVideoMemoryBytes;

    /** @brief PCI Vendor ID */
    std::uint32_t vendorId;

    /** @brief PCI Device ID */
    std::uint32_t deviceId;

    /** @brief 選択されたBackendの実装種別 */
    GraphicsBackendKind backendKind;

    /** @brief 選択されたAdapterの種別 */
    GraphicsAdapterKind adapterKind;

    /** @brief 提供されるGraphics Profile */
    GraphicsProfile profile;

    /** @brief Unified Memory Architectureの場合はtrue */
    bool isUma;
};

/**
 * @brief Platform非依存のGraphics Backend所有契約
 *
 * Backendは生成Thread上で一意所有し、全ての公開操作とDestructorを同じThread上で呼び出す
 *
 * `Unavailable`は安全なResource解放を証明できないProcess終端状態であり、Ownerを破棄しない
 */
class GraphicsBackend
{
  public:
    /**
     * @brief 明示Shutdown後にBackend Ownerを破棄する
     *
     * `Shutdown`、またはGPU Workを投入していない初期化失敗状態だけで破棄できる
     */
    virtual ~GraphicsBackend() noexcept;

    GraphicsBackend(const GraphicsBackend &) = delete;
    GraphicsBackend &operator=(const GraphicsBackend &) = delete;

    /**
     * @brief Backendが保持するCapability Reportを返す
     * @return Backend破棄開始まで有効なImmutable参照
     */
    [[nodiscard]] virtual const CapabilityReport &capabilities() const noexcept = 0;

    /** @brief 現在のBackend Lifecycle状態を返す */
    [[nodiscard]] virtual GraphicsBackendState state() const noexcept = 0;

    /**
     * @brief Backendを安全に停止する
     * @return 停止成功、または診断可能な停止Error
     *
     * `Shutdown`では成功する冪等操作とする
     * `Unavailable`ではResourceを解放せず`RHI.BackendUnavailable`を返す
     * 有効なPresentation Contextが残る場合は副作用なく`RHI.ActivePresentationContexts`を返す
     */
    [[nodiscard]] virtual Result<void> shutdown() noexcept = 0;

  protected:
    GraphicsBackend() = default;
};
} // namespace cue
