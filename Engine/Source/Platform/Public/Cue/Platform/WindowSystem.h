#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/Window.h>

#include <memory>
#include <string_view>

namespace cue
{
/** @brief Window 生成時の Platform 非依存 Descriptor */
struct WindowDescriptor final
{
    std::string_view title;
    WindowSize clientSize;
};

/** @brief Non-blocking Message Pump の結果 */
enum class PumpStatus
{
    Running,
    QuitRequested,
};

/**
 * @brief Platform 非依存の Window System 契約
 *
 * 全 API と Destructor は作成 Thread 上で呼び出し、生成した全 Window より長く生存させる
 */
class WindowSystem
{
  public:
    /** @brief Window Thread 上で Owner を破棄する */
    virtual ~WindowSystem() noexcept;

    /** @brief Window の一意な所有権を生成する */
    [[nodiscard]] virtual Result<std::unique_ptr<Window>> create_window(
        const WindowDescriptor &a_descriptor) noexcept = 0;

    /** @brief 現在の Thread Queue を Non-blocking で処理する */
    [[nodiscard]] virtual Result<PumpStatus> pump_events() noexcept = 0;
};
} // namespace cue
