#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/WindowSystem.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr int k_invalidArguments = 64;
constexpr int k_systemCreationFailed = 1;
constexpr int k_windowCreationFailed = 2;
constexpr int k_windowShowFailed = 3;
constexpr int k_messagePumpFailed = 4;
constexpr int k_windowDestroyFailed = 5;

struct RuntimeOptions final
{
    std::string title = "CueEngine Runtime Host";
    cue::WindowSize clientSize = {1280, 720};
    bool isSmokeTest = false;
};

[[nodiscard]] bool convert_title(std::wstring_view a_text, std::string &a_result)
{
    if (a_text.empty())
    {
        a_result.clear();
        return true;
    }

    if (a_text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    int sourceLength = static_cast<int>(a_text.size());
    int convertedLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), sourceLength, nullptr, 0,
                                              nullptr, nullptr);

    if (convertedLength == 0)
    {
        return false;
    }

    std::string result(static_cast<std::size_t>(convertedLength), '\0');
    int writtenLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), sourceLength, result.data(),
                                            convertedLength, nullptr, nullptr);

    if (writtenLength != convertedLength)
    {
        return false;
    }

    a_result = std::move(result);
    return true;
}

[[nodiscard]] bool parse_size(std::wstring_view a_text, std::uint32_t &a_value) noexcept
{
    if (a_text.empty())
    {
        return false;
    }

    std::uint32_t value = 0;

    for (wchar_t character : a_text)
    {
        if (character < L'0' || character > L'9')
        {
            return false;
        }

        std::uint32_t digit = static_cast<std::uint32_t>(character - L'0');

        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
        {
            return false;
        }

        value = value * 10 + digit;
    }

    if (value == 0)
    {
        return false;
    }

    a_value = value;
    return true;
}

[[nodiscard]] bool parse_options(int a_argumentCount, wchar_t **a_arguments, RuntimeOptions &a_options)
{
    for (int index = 1; index < a_argumentCount; ++index)
    {
        std::wstring_view argument = a_arguments[index];

        if (argument == L"--smoke-test")
        {
            a_options.isSmokeTest = true;
            continue;
        }

        if (index + 1 >= a_argumentCount)
        {
            return false;
        }

        std::wstring_view value = a_arguments[++index];

        if (argument == L"--title")
        {
            if (!convert_title(value, a_options.title))
            {
                return false;
            }
        }
        else if (argument == L"--width")
        {
            if (!parse_size(value, a_options.clientSize.width))
            {
                return false;
            }
        }
        else if (argument == L"--height")
        {
            if (!parse_size(value, a_options.clientSize.height))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    return true;
}

void print_usage() noexcept
{
    std::fputws(L"Usage: CueRuntimeHost [--smoke-test] [--title <title>] [--width <pixels>] [--height <pixels>]\n",
                stderr);
}

[[nodiscard]] int report_error(cue::Logger &a_logger, std::string_view a_message, cue::Error &&a_error,
                               int a_exitCode) noexcept
{
    static_cast<void>(a_logger.log(cue::LogLevel::Error, a_message, std::move(a_error)));
    static_cast<void>(a_logger.flush());
    return a_exitCode;
}

[[nodiscard]] int run(const RuntimeOptions &a_options, cue::AbortFatalHandler &a_fatalHandler)
{
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<cue::ConsoleLogSink>());
    cue::Logger logger(a_fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, a_fatalHandler);

    static_cast<void>(logger.log(cue::LogLevel::Info, "Runtime Host initialization started"));
    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(assertContext);

    if (!systemResult)
    {
        return report_error(logger, "Runtime Host failed to create Window System", std::move(*systemResult.try_error()),
                            k_systemCreationFailed);
    }

    std::unique_ptr<cue::WindowSystem> windowSystem = std::move(*systemResult.try_value());
    cue::WindowDescriptor descriptor = {a_options.title, a_options.clientSize};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = windowSystem->create_window(descriptor);

    if (!windowResult)
    {
        return report_error(logger, "Runtime Host failed to create Window", std::move(*windowResult.try_error()),
                            k_windowCreationFailed);
    }

    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());
    cue::Result<void> showResult = window->show();

    if (!showResult)
    {
        return report_error(logger, "Runtime Host failed to show Window", std::move(*showResult.try_error()),
                            k_windowShowFailed);
    }

    static_cast<void>(logger.log(cue::LogLevel::Info, "Runtime Host Main Loop started"));
    bool isShutdownRequested = a_options.isSmokeTest;

    while (true)
    {
        cue::Result<cue::PumpStatus> pumpResult = windowSystem->pump_events();

        if (!pumpResult)
        {
            return report_error(logger, "Runtime Host Message Pump failed", std::move(*pumpResult.try_error()),
                                k_messagePumpFailed);
        }

        bool isQuitRequested = *pumpResult.try_value() == cue::PumpStatus::QuitRequested;
        bool hasEvent = false;
        cue::WindowEvent event = {};

        while (window->try_pop_event(event))
        {
            hasEvent = true;

            if (event.type == cue::WindowEventType::CloseRequested || event.type == cue::WindowEventType::Destroyed)
            {
                isShutdownRequested = true;
            }
        }

        if (isQuitRequested)
        {
            isShutdownRequested = true;
        }

        bool didDestroy = false;

        if (isShutdownRequested && window->state() != cue::WindowState::Destroyed)
        {
            cue::Result<void> destroyResult = window->destroy();

            if (!destroyResult)
            {
                return report_error(logger, "Runtime Host failed to destroy Window",
                                    std::move(*destroyResult.try_error()), k_windowDestroyFailed);
            }

            didDestroy = true;
        }

        if (isQuitRequested && window->state() == cue::WindowState::Destroyed && !didDestroy)
        {
            break;
        }

        if (!hasEvent && !isShutdownRequested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    window.reset();
    windowSystem.reset();
    static_cast<void>(logger.log(cue::LogLevel::Info, "Runtime Host shutdown completed"));
    static_cast<void>(logger.flush());
    return 0;
}
} // namespace

int wmain(int a_argumentCount, wchar_t **a_arguments)
{
    cue::AbortFatalHandler fatalHandler;

    try
    {
        RuntimeOptions options;

        if (!parse_options(a_argumentCount, a_arguments, options))
        {
            print_usage();
            return k_invalidArguments;
        }

        return run(options, fatalHandler);
    }
    catch (...)
    {
        fatalHandler.terminate("Runtime Host allocation failed");
    }
}
