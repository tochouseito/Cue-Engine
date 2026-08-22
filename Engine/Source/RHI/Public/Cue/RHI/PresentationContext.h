#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>

namespace cue
{
/** @brief Presentation Context の Lifecycle 状態 */
enum class PresentationContextState
{
    Ready,
    DeviceRemoved,
    Unavailable,
    Shutdown,
};

/** @brief Platform 非依存の Presentation 生成設定 */
struct PresentationDescriptor final
{
    /** @brief 垂直同期を有効にする場合は true */
    bool isVsyncEnabled;
};

/**
 * @brief Window に対応する Presentation Resource の一意所有契約
 *
 * Context は生成 Thread 上でのみ操作し、Backend と Window より先に shutdown して破棄する。
 * 現段階の Context 生成は GPU Work を submit しない。将来 Frame submit を追加する際は、
 * shutdown 前に Context 所有 Work の terminal Fence 完了を保証する
 */
class PresentationContext
{
  public:
    /** @brief 明示 shutdown 後に Owner を破棄する */
    virtual ~PresentationContext() noexcept;

    PresentationContext(const PresentationContext &) = delete;
    PresentationContext &operator=(const PresentationContext &) = delete;

    /** @brief 現在の Lifecycle 状態を返す */
    [[nodiscard]] virtual PresentationContextState state() const noexcept = 0;

    /** @brief Swap Chain の幅を返す */
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;

    /** @brief Swap Chain の高さを返す */
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;

    /** @brief Back Buffer 数を返す */
    [[nodiscard]] virtual std::uint32_t buffer_count() const noexcept = 0;

    /** @brief DXGI が報告した現在の Back Buffer Index を返す */
    [[nodiscard]] virtual std::uint32_t current_back_buffer_index() const noexcept = 0;

    /** @brief VSync 設定を返す */
    [[nodiscard]] virtual bool is_vsync_enabled() const noexcept = 0;

    /** @brief Tearing capability の有無を返す */
    [[nodiscard]] virtual bool is_tearing_supported() const noexcept = 0;

    /** @brief 現在の Presentation 設定で Tearing を使用する場合は true */
    [[nodiscard]] virtual bool is_tearing_enabled() const noexcept = 0;

    /**
     * @brief Presentation Resource を安全に停止して Backend 登録を解除する
     *
     * Shutdown 状態での再呼出は成功する。Unavailable 状態では Resource と Backend 登録を保持して
     * Error を返す。DeviceRemoved 状態では Backend の DRED 診断を Native Resource 解放より先に
     * 一度試行し、診断が失敗しても解放と登録解除を完遂した後にその Error を返す
     *
     * @return 停止成功、または診断可能な停止 Error
     */
    [[nodiscard]] virtual Result<void> shutdown() noexcept = 0;

  protected:
    PresentationContext() = default;
};
} // namespace cue
