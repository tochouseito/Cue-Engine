#pragma once

#include <Cue/Foundation/Result.h>

#include <array>
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

/** @brief 最小Presentation Frameの入力 */
struct PresentationFrameDescriptor final
{
    /** @brief Back Bufferへ書き込むRGBA Clear Color */
    std::array<float, 4> clearColor;
};

/** @brief Presentation結果 */
enum class PresentationFrameStatus
{
    Presented,
    Occluded,
};

/**
 * @brief Window に対応する Presentation Resource の一意所有契約
 *
 * Context は生成 Thread 上でのみ操作し、Backend と Window より先に shutdown して破棄する。
 * ContextがsubmitしたGPU WorkはFrame ContextのFenceで追跡し、shutdown前にterminal Fence完了を保証する
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

    /** @brief 0 Size のため Resize が延期されている場合は true */
    [[nodiscard]] virtual bool is_resize_pending() const noexcept = 0;

    /**
     * @brief Back BufferをClearしてSubmit、Present、Signalする
     *
     * Current Back Bufferの再利用Fence完了を待ってからCommandを記録する。
     * Present成功後と非Device Removal失敗後はExecute済みWorkを覆うFenceをSignalする。
     * Present失敗またはSignal回収後のErrorでは新しいFrame受付を停止する。
     * Device RemovalではDRED後にFence Waitなしで解放し、GPU完了を証明できない場合はUnavailableとして
     * Native Resourceを保持する。Native同期値は公開せず、成功時は表示結果だけを返す。
     */
    [[nodiscard]] virtual Result<PresentationFrameStatus> present_frame(
        const PresentationFrameDescriptor &a_descriptor) noexcept = 0;

    /**
     * @brief Presentation Resource を指定 Size へ再構築する
     *
     * Width または Height が 0 の場合は Native Resize を行わず延期する。同一 Size は no-op とする。
     * 再構築前に Context が使用する GPU Work の完了を有限時間で待機する
     *
     * GPU完了を証明できない場合はUnavailableへ遷移し、Native ResourceとBackend登録を保持する。
     * GPU完了後のCommand再初期化、ResizeBuffers、Back Buffer再取得、RTV再構築に失敗した場合は、
     * Native Resourceを規定順で解放してBackend登録を解除し、Shutdownへ遷移する。
     * Device Removalを検出した場合はDRED収集をNative Resource解放より先に一度試行し、
     * 診断結果をErrorへ保持したうえで登録解除とShutdownを完遂する。
     * Resize以外のErrorでFrame受付を停止したContextは再開せず、Resourceと登録を保持してErrorを返す。
     *
     * @return 再構築または延期の成功、もしくは診断可能な Resize Error
     */
    [[nodiscard]] virtual Result<void> resize(std::uint32_t a_width, std::uint32_t a_height) noexcept = 0;

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
