#include <Cue/Foundation/Assert.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
std::atomic<int> breakCount = 0;

struct AssertState
{
    bool didWriteFatal = false;
};

void try_break() noexcept
{
    ++breakCount;
}

class AssertSink final : public cue::LogSink
{
  public:
    explicit AssertSink(AssertState &a_state) noexcept : m_state(a_state)
    {
    }

    [[nodiscard]] bool write(const cue::LogRecord &a_record) override
    {
        m_state.didWriteFatal = a_record.level() == cue::LogLevel::Fatal;
        return true;
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }

  private:
    AssertState &m_state;
};

class AssertFatalHandler final : public cue::FatalHandler
{
  public:
    explicit AssertFatalHandler(AssertState &a_state) noexcept : m_state(a_state)
    {
    }

    [[noreturn]] void terminate() noexcept override
    {
        constexpr int k_expectedBreakCount = CUE_ENABLE_DEBUG_BREAK ? 1 : 0;
        const bool isValid = m_state.didWriteFatal && breakCount == k_expectedBreakCount;
        std::_Exit(isValid ? 75 : 77);
    }

    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }

  private:
    AssertState &m_state;
};

[[nodiscard]] int run_success()
{
    AssertState state;
    AssertFatalHandler handler(state);
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<AssertSink>(state));
    cue::Logger logger(handler, std::move(sinks));
    cue::AssertContext context(logger, handler, try_break);

    int conditionEvaluationCount = 0;
    CUE_ASSERT(context, ++conditionEvaluationCount == 1, "success");
    constexpr int k_expectedEvaluationCount = CUE_ENABLE_ASSERTS ? 1 : 0;
    return conditionEvaluationCount == k_expectedEvaluationCount && !state.didWriteFatal ? 0 : 1;
}

[[nodiscard]] int run_failure()
{
    AssertState state;
    AssertFatalHandler handler(state);
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<AssertSink>(state));
    cue::Logger logger(handler, std::move(sinks));
    cue::AssertContext context(logger, handler, try_break);

    CUE_ASSERT(context, false, "assert failure");
    return 78;
}
} // namespace

int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 10;
    }
    const std::string_view mode = a_arguments[1];
    if (mode == "Success")
    {
        return run_success();
    }
    if (mode == "Failure")
    {
        return run_failure();
    }
    return 11;
}
