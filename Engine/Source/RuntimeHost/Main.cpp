#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/WindowSystem.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <system_error>
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
    std::string_view title = "CueEngine Runtime Host";
    cue::WindowSize clientSize = {1280, 720};
    bool isSmokeTest = false;
};

[[nodiscard]] bool parse_size(std::string_view a_text, std::uint32_t &a_value) noexcept
{
    std::uint32_t value = 0;
    const char *begin = a_text.data();
    const char *end = begin + a_text.size();
    std::from_chars_result result = std::from_chars(begin, end, value);

    if (result.ec != std::errc() || result.ptr != end || value == 0)
    {
        return false;
    }

    a_value = value;
    return true;
}

[[nodiscard]] bool parse_options(int a_argumentCount, char **a_arguments, RuntimeOptions &a_options) noexcept
{
    for (int index = 1; index < a_argumentCount; ++index)
    {
        std::string_view argument = a_arguments[index];

        if (argument == "--smoke-test")
        {
            a_options.isSmokeTest = true;
            continue;
        }

        if (index + 1 >= a_argumentCount)
        {
            return false;
        }

        std::string_view value = a_arguments[++index];

        if (argument == "--title")
        {
            a_options.title = value;
        }
        else if (argument == "--width")
        {
            if (!parse_size(value, a_options.clientSize.width))
            {
                return false;
            }
        }
        else if (argument == "--height")
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
    std::fputs("Usage: CueRuntimeHost [--smoke-test] [--title <UTF-8 title>] [--width <pixels>] [--height <pixels>]\n",
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

int main(int a_argumentCount, char **a_arguments)
{
    RuntimeOptions options;

    if (!parse_options(a_argumentCount, a_arguments, options))
    {
        print_usage();
        return k_invalidArguments;
    }

    cue::AbortFatalHandler fatalHandler;

    try
    {
        return run(options, fatalHandler);
    }
    catch (...)
    {
        fatalHandler.terminate("Runtime Host allocation failed");
    }
}
