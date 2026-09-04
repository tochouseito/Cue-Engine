#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/Windows/WindowsMessageSink.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>
#include <Cue/Platform/Windows/WindowsWindowInterop.h>

#include <Windows.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 契約違反をTest Processの固定Exit Codeへ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(75);
    }

    /// @brief Message付き契約違反をTest Processの固定Exit Codeへ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }
};

class ForeignWindow final : public cue::Window
{
  public:
    /// @brief Test用異種Windowを表示済みとして扱う
    [[nodiscard]] cue::Result<void> show() noexcept override
    {
        return cue::Result<void>::success();
    }

    /// @brief Test用異種Windowを破棄済みとして扱う
    [[nodiscard]] cue::Result<void> destroy() noexcept override
    {
        return cue::Result<void>::success();
    }

    /// @brief Message Sink対象外のWindow Stateを返す
    [[nodiscard]] cue::WindowState state() const noexcept override
    {
        return cue::WindowState::Created;
    }

    /// @brief Message Sink対象外のWindow Sizeを返す
    [[nodiscard]] cue::WindowSize client_size() const noexcept override
    {
        return {320, 180};
    }

    /// @brief Message Sink対象外WindowがEventを持たないことを返す
    [[nodiscard]] bool try_pop_event(cue::WindowEvent &) noexcept override
    {
        return false;
    }
};

class RecordingSink final : public cue::WindowsMessageSink
{
  public:
    /// @brief 次に期待するMessageと応答を設定する
    void expect(const void *a_window, std::uint32_t a_message, std::uintptr_t a_wordParameter,
                std::intptr_t a_longParameter, cue::WindowsMessageResult a_result) noexcept
    {
        m_expectedWindow = a_window;
        m_expectedMessage = a_message;
        m_expectedWordParameter = a_wordParameter;
        m_expectedLongParameter = a_longParameter;
        m_result = a_result;
        m_didMatch = false;
    }

    /// @brief 配送値を期待値と同期比較し、設定済みNative Resultを返す
    [[nodiscard]] cue::WindowsMessageResult process_message(const cue::WindowsMessageView &a_message) noexcept override
    {
        ++m_callCount;
        m_didMatch = a_message.nativeWindow == m_expectedWindow && a_message.message == m_expectedMessage &&
                     a_message.wordParameter == m_expectedWordParameter &&
                     a_message.longParameter == m_expectedLongParameter;
        return m_result;
    }

    /// @brief Callback呼出し回数を返す
    [[nodiscard]] std::size_t call_count() const noexcept
    {
        return m_callCount;
    }

    /// @brief 最後のCallbackが設定済み期待値と一致したか返す
    [[nodiscard]] bool did_match() const noexcept
    {
        return m_didMatch;
    }

  private:
    const void *m_expectedWindow = nullptr;
    std::uint32_t m_expectedMessage = 0;
    std::uintptr_t m_expectedWordParameter = 0;
    std::intptr_t m_expectedLongParameter = 0;
    cue::WindowsMessageResult m_result = {false, 0};
    std::size_t m_callCount = 0;
    bool m_didMatch = false;
};

/// @brief Test用Loggerを追加Sinkなしで生成する
[[nodiscard]] std::unique_ptr<cue::Logger> create_logger(TestFatalHandler &a_handler)
{
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    return std::make_unique<cue::Logger>(a_handler, std::move(sinks));
}

/// @brief Message Sink Errorが安定DomainとCodeを保持するか判定する
[[nodiscard]] bool has_sink_error(const cue::Result<void> &a_result, cue::WindowsMessageSinkError a_error) noexcept
{
    return !a_result && a_result.try_error() != nullptr &&
           a_result.try_error()->code().domain() == "Cue.Platform.Windows" &&
           a_result.try_error()->code().value() == static_cast<std::int64_t>(a_error);
}

/// @brief Message Sinkの関連付け、配送、Lifecycle優先、解除契約を検証する
[[nodiscard]] bool test_message_sink(cue::AssertContext &a_context)
{
    RecordingSink firstSink;
    RecordingSink secondSink;
    ForeignWindow foreignWindow;
    cue::Result<void> foreignResult = cue::attach_windows_message_sink(foreignWindow, firstSink, a_context);
    if (!has_sink_error(foreignResult, cue::WindowsMessageSinkError::InvalidWindowKind))
    {
        return false;
    }

    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(a_context);
    if (!systemResult)
    {
        return false;
    }
    std::unique_ptr<cue::WindowSystem> system = std::move(*systemResult.try_value());
    cue::WindowDescriptor descriptor = {"Message Sink Test", {640, 360}};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = system->create_window(descriptor);
    if (!windowResult)
    {
        return false;
    }
    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());
    if (!window->show())
    {
        return false;
    }

    cue::Result<cue::NativeWindowView> nativeViewResult = cue::get_native_window_view(*window, a_context);
    if (!nativeViewResult)
    {
        return false;
    }
    const void *nativeValue = nativeViewResult.try_value()->value();
    HWND nativeWindow = static_cast<HWND>(const_cast<void *>(nativeValue));
    if (nativeWindow == nullptr)
    {
        return false;
    }

    cue::Result<void> attached = cue::attach_windows_message_sink(*window, firstSink, a_context);
    cue::Result<void> repeatedAttach = cue::attach_windows_message_sink(*window, firstSink, a_context);
    cue::Result<void> conflictingAttach = cue::attach_windows_message_sink(*window, secondSink, a_context);
    if (!attached || !repeatedAttach ||
        !has_sink_error(conflictingAttach, cue::WindowsMessageSinkError::MessageSinkAlreadyAttached))
    {
        return false;
    }

    constexpr std::intptr_t k_expectedResult = 91;
    firstSink.expect(nativeValue, WM_KEYDOWN, VK_RETURN, 123, {true, k_expectedResult});
    LRESULT result = SendMessageW(nativeWindow, WM_KEYDOWN, VK_RETURN, 123);
    if (result != k_expectedResult || firstSink.call_count() != 1 || !firstSink.did_match() ||
        secondSink.call_count() != 0)
    {
        return false;
    }

    cue::Result<void> mismatchedDetach = cue::detach_windows_message_sink(*window, secondSink, a_context);
    if (!has_sink_error(mismatchedDetach, cue::WindowsMessageSinkError::MessageSinkMismatch))
    {
        return false;
    }

    firstSink.expect(nativeValue, WM_SIZE, SIZE_MINIMIZED, 0, {true, 47});
    const std::size_t callsBeforeLifecycle = firstSink.call_count();
    result = SendMessageW(nativeWindow, WM_SIZE, SIZE_MINIMIZED, 0);
    if (result != 0 || firstSink.call_count() != callsBeforeLifecycle)
    {
        return false;
    }

    result = SendMessageW(nativeWindow, WM_CLOSE, 0, 0);
    if (result != 0 || firstSink.call_count() != callsBeforeLifecycle ||
        window->state() != cue::WindowState::CloseRequested)
    {
        return false;
    }

    cue::Result<void> detached = cue::detach_windows_message_sink(*window, firstSink, a_context);
    cue::Result<void> repeatedDetach = cue::detach_windows_message_sink(*window, firstSink, a_context);
    if (!detached || !repeatedDetach)
    {
        return false;
    }

    SendMessageW(nativeWindow, WM_KEYDOWN, VK_ESCAPE, 0);
    if (firstSink.call_count() != callsBeforeLifecycle)
    {
        return false;
    }

    return window->destroy().has_value();
}
} // namespace

/// @brief Windows Message Sinkの公開状態遷移とNative配送をProcess単位で検証する
int main()
{
    TestFatalHandler handler;
    std::unique_ptr<cue::Logger> logger = create_logger(handler);
    cue::AssertContext context(*logger, handler);
    return test_message_sink(context) ? 0 : 1;
}
