#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/RHI/PresentationContext.h>

#include <memory>

namespace cue
{
class D3d12Backend;
class Window;

/**
 * @brief Windows Window と D3D12 Backend を接続する Presentation Context を生成する
 * @param a_backend Context より長く生存する D3D12 Backend
 * @param a_window Context より長く生存する Windows Window
 * @param a_descriptor Platform 非依存の Presentation 設定
 * @return 成功時は Presentation Context の一意 Owner、失敗時は診断可能な Error
 */
[[nodiscard]] Result<std::unique_ptr<PresentationContext>> create_d3d12_windows_presentation(
    D3d12Backend &a_backend, Window &a_window, const PresentationDescriptor &a_descriptor) noexcept;
} // namespace cue
