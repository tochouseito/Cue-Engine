#include <Cue/RHI/D3D12/Windows/D3d12WindowsPresentation.h>

#include <Cue/Platform/Window.h>
#include <Cue/Platform/Windows/WindowsWindowInterop.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <utility>

namespace cue
{
// Windows 固有の Native Window 抽出をこの接続境界に閉じ込める
// Platform 非依存の上位 RHI API へ Windows 型を露出させないための Adapter とする
class D3d12WindowsPresentationAccess final
{
  public:
    /// @brief D3D12 Windows Presentation が保持する Assert Context を呼び出し元へ返す
    [[nodiscard]] static const AssertContext &assert_context(const D3d12Backend &a_backend) noexcept
    {
        return a_backend.assert_context_for_presentation();
    }

    /// @brief D3D12 Backend と Native Window から Windows Presentation Context を生成する
    [[nodiscard]] static Result<std::unique_ptr<PresentationContext>> create(
        D3d12Backend &a_backend, const NativeWindowView &a_window, WindowSize a_size,
        const PresentationDescriptor &a_descriptor) noexcept
    {
        return a_backend.create_windows_presentation(a_window.value(), a_size.width, a_size.height, a_descriptor);
    }
};

Result<std::unique_ptr<PresentationContext>> create_d3d12_windows_presentation(
    D3d12Backend &a_backend, Window &a_window, const PresentationDescriptor &a_descriptor) noexcept
{
    const WindowSize size = a_window.client_size();
    // NativeWindowView は Window の所有権や寿命を延長しない
    // 生成処理へ Native Window 値を同期的に渡す短命 View としてだけ使用する
    Result<NativeWindowView> windowResult =
        get_native_window_view(a_window, D3d12WindowsPresentationAccess::assert_context(a_backend));

    if (!windowResult)
    {
        return Result<std::unique_ptr<PresentationContext>>::failure(std::move(*windowResult.try_error()));
    }

    const NativeWindowView &window = *windowResult.try_value();
    return D3d12WindowsPresentationAccess::create(a_backend, window, size, a_descriptor);
}
} // namespace cue
