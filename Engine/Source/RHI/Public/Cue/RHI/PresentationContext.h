#pragma once

#include <Cue/Foundation/Result.h>

#include <array>
#include <cstdint>

namespace cue
{
/** @brief Owner が許可する操作と破棄可否を判断する Presentation Context の Lifecycle 状態 */
enum class PresentationContextState
{
    /** @brief Native Resource を保持し、個別の受付 Flag と併せて操作可否を判断する状態 */
    Ready,

    /** @brief Device Removal 後に診断と制御された解放だけを許可する状態 */
    DeviceRemoved,

    /** @brief GPU 完了を証明できず、Native Resource と Backend 登録を保持する状態 */
    Unavailable,

    /** @brief Native Resource の解放と Backend 登録解除が完了し、Owner を破棄できる状態 */
    Shutdown,
};

/** @brief Platform 固有の Window 情報と分離して Presentation 動作を指定する生成設定 */
struct PresentationDescriptor final
{
    /** @brief 垂直同期を有効にする場合は true */
    bool isVsyncEnabled;
};

/** @brief Renderer 非依存の最小 Frame を表示経路へ投入するための入力 */
struct PresentationFrameDescriptor final
{
    /** @brief Back Buffer へ書き込む RGBA Clear Color */
    std::array<float, 4> clearColor;
};

/** @brief Frame 投入の成功を表示可否と分けて通知する Presentation 結果 */
enum class PresentationFrameStatus
{
    /** @brief Frame が表示経路へ正常に提示された */
    Presented,

    /** @brief Present 対象が表示されなかったが Frame 投入と同期処理は正常に完了した */
    Occluded,
};

/**
 * @brief Window に対応する Presentation Resource の一意所有契約
 *
 * Context は生成 Thread 上でのみ操作し、Backend と Window より先に shutdown して破棄する
 * 通常 shutdown 成功時は terminal Fence 完了を保証し、Device Removal 時は Fence 完了を要求せず DRED 診断後に解放する
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

    /** @brief 確立済み Swap Chain の幅を返し、0 Size の Resize 延期中は直前の幅を維持する */
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;

    /** @brief 確立済み Swap Chain の高さを返し、0 Size の Resize 延期中は直前の高さを維持する */
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;

    /** @brief Back Buffer 数を返す */
    [[nodiscard]] virtual std::uint32_t buffer_count() const noexcept = 0;

    /** @brief 次の Frame が使用する現在の Back Buffer Index を返す */
    [[nodiscard]] virtual std::uint32_t current_back_buffer_index() const noexcept = 0;

    /** @brief VSync 設定を返す */
    [[nodiscard]] virtual bool is_vsync_enabled() const noexcept = 0;

    /** @brief 実行環境が Tearing を利用できるかを返す */
    [[nodiscard]] virtual bool is_tearing_supported() const noexcept = 0;

    /** @brief Capability と VSync 設定を反映した結果、現在の Present で Tearing を使用する場合は true */
    [[nodiscard]] virtual bool is_tearing_enabled() const noexcept = 0;

    /** @brief 0 Size のため Resize が延期されている場合は true */
    [[nodiscard]] virtual bool is_resize_pending() const noexcept = 0;

    /**
     * @brief Back Buffer を Clear して Submit、Present、Signal する
     *
     * Current Back Buffer の再利用 Fence 完了を待ってから Command を記録する
     * Present 成功後と非 Device Removal 失敗後は Execute 済み Work を覆う Fence を Signal する
     * Present 失敗または Signal 回収後の Error では新しい Frame 受付を停止する
     * Device Removal では Frame 受付を停止して DRED 診断を準備し、Native Resource 解放は後続の shutdown へ委ねる
     * GPU 完了を証明できない場合は Unavailable として Native Resource を保持する
     * Native 同期値は公開せず、成功時は表示結果だけを返す
     * Ready 以外、Backend が Ready 以外、または 0 Size の Resize 延期中は Frame を投入しない
     */
    [[nodiscard]] virtual Result<PresentationFrameStatus> present_frame(
        const PresentationFrameDescriptor &a_descriptor) noexcept = 0;

    /**
     * @brief Presentation Resource を指定 Size へ再構築する
     *
     * Width または Height が 0 の場合は Native Resize を行わず延期し、同一 Size は no-op とする
     * 再構築前に Context が使用する GPU Work の完了を有限時間で待機する
     * Frame 境界以外では Resource を変更せず Error を返し、延期中は present_frame を拒否し、
     * 次の有効 Size で Resource を再構築または Frame 受付を再開する
     *
     * GPU 完了を証明できない場合は Unavailable へ遷移し、Native Resource と Backend 登録を保持する
     * GPU 完了後の Command 再初期化、ResizeBuffers、Back Buffer 再取得、RTV 再構築に失敗した場合は、
     * Native Resource を規定順で解放して Backend 登録を解除し、Shutdown へ遷移する
     * Device Removal を検出した場合は DRED 収集を Native Resource 解放より先に一度試行し、
     * 診断結果を Error へ保持したうえで登録解除と Shutdown を完遂する
     * Resize 以外の Error で Frame 受付を停止した Context は再開せず、Resource と登録を保持して Error を返す
     *
     * @return 再構築または延期の成功、もしくは診断可能な Resize Error
     */
    [[nodiscard]] virtual Result<void> resize(std::uint32_t a_width, std::uint32_t a_height) noexcept = 0;

    /**
     * @brief Presentation Resource を安全に停止して Backend 登録を解除する
     *
     * Shutdown 状態での再呼出は成功し、Unavailable 状態では Resource と Backend 登録を保持して
     * Error を返し、DeviceRemoved 状態では Backend の DRED 診断を Native Resource 解放より先に
     * 一度試行し、診断が失敗しても解放と登録解除を完遂した後にその Error を返す
     *
     * @return 停止成功、または診断可能な停止 Error
     */
    [[nodiscard]] virtual Result<void> shutdown() noexcept = 0;

  protected:
    PresentationContext() = default;
};
} // namespace cue
