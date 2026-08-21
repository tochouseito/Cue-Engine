#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>

#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr int k_fatalExitCode = 75;
constexpr int k_emergencyExitCode = 76;

struct SinkState
{
    bool didWrite = false;
    bool didFlush = false;
};

class TestFatalHandler final : public cue::FatalHandler
{
  public:
    explicit TestFatalHandler(SinkState *a_state = nullptr) noexcept : m_state(a_state)
    {
    }

    [[noreturn]] void terminate() noexcept override
    {
        const bool isValid = m_state == nullptr || (m_state->didWrite && m_state->didFlush);
        std::_Exit(isValid ? k_fatalExitCode : 77);
    }

    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(k_emergencyExitCode);
    }

  private:
    SinkState *m_state;
};

class StateSink final : public cue::LogSink
{
  public:
    StateSink(SinkState &a_state, bool a_shouldFail) noexcept : m_state(a_state), m_shouldFail(a_shouldFail)
    {
    }

    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        m_state.didWrite = true;
        return !m_shouldFail;
    }

    [[nodiscard]] bool flush() override
    {
        m_state.didFlush = true;
        return !m_shouldFail;
    }

  private:
    SinkState &m_state;
    bool m_shouldFail;
};

class ThrowingSink final : public cue::LogSink
{
  public:
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        throw std::runtime_error("sink failure");
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

class ReentrantSink final : public cue::LogSink
{
  public:
    void set_logger(cue::Logger &a_logger) noexcept
    {
        m_logger = &a_logger;
    }

    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        [[maybe_unused]] const cue::LogResult result = m_logger->log(cue::LogLevel::Info, "reentry");
        return true;
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }

  private:
    cue::Logger *m_logger = nullptr;
};

class BlockingSink final : public cue::LogSink
{
  public:
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        std::unique_lock lock(m_mutex);
        m_didEnter = true;
        m_condition.notify_all();
        m_condition.wait(lock, [this]() { return m_canExit; });
        return true;
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }

    void wait_until_entered()
    {
        std::unique_lock lock(m_mutex);
        m_condition.wait(lock, [this]() { return m_didEnter; });
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_didEnter = false;
    bool m_canExit = false;
};

[[noreturn]] void run_fatal(bool a_shouldSinkFail)
{
    SinkState state;
    TestFatalHandler handler(&state);
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<StateSink>(state, a_shouldSinkFail));
    cue::Logger logger(handler, std::move(sinks));
    cue::report_fatal(logger, handler, "fatal test");
}

[[noreturn]] void run_throwing_sink()
{
    TestFatalHandler handler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<ThrowingSink>());
    cue::Logger logger(handler, std::move(sinks));
    [[maybe_unused]] const cue::LogResult result = logger.log(cue::LogLevel::Info, "throw");
    std::_Exit(78);
}

[[noreturn]] void run_reentry()
{
    TestFatalHandler handler;
    auto sink = std::make_unique<ReentrantSink>();
    ReentrantSink *sinkPointer = sink.get();
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::move(sink));
    cue::Logger logger(handler, std::move(sinks));
    sinkPointer->set_logger(logger);
    [[maybe_unused]] const cue::LogResult result = logger.log(cue::LogLevel::Info, "outer");
    std::_Exit(78);
}

[[noreturn]] void run_fatal_contended()
{
    TestFatalHandler handler;
    auto sink = std::make_unique<BlockingSink>();
    BlockingSink *sinkPointer = sink.get();
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::move(sink));
    cue::Logger logger(handler, std::move(sinks));

    std::thread loggingThread(
        [&logger]() { [[maybe_unused]] const cue::LogResult result = logger.log(cue::LogLevel::Info, "blocking"); });
    sinkPointer->wait_until_entered();
    cue::report_fatal(logger, handler, "contended fatal");
}
} // namespace

int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 10;
    }

    const std::string_view mode = a_arguments[1];
    if (mode == "Fatal")
    {
        run_fatal(false);
    }
    if (mode == "FatalSinkFailure")
    {
        run_fatal(true);
    }
    if (mode == "ThrowingSink")
    {
        run_throwing_sink();
    }
    if (mode == "Reentry")
    {
        run_reentry();
    }
    if (mode == "FatalContended")
    {
        run_fatal_contended();
    }
    return 11;
}
