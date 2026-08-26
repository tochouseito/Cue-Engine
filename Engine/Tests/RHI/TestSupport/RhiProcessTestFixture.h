#pragma once

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Log.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace cue::test
{
class ProcessFatalHandler final : public FatalHandler
{
  public:
    /// @brief Message を伴わない回復不能失敗を固定 Exit Code で Process 終了へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    /// @brief Message を伴う回復不能失敗を固定 Exit Code で Process 終了へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

class AcceptingLogSink final : public LogSink
{
  public:
    /// @brief Process Test が診断出力の成否に依存しない場合に Log Record を受理する
    [[nodiscard]] bool write(const LogRecord &) override
    {
        return true;
    }

    /// @brief 保留出力を持たない Test Sink の Flush 成功を返す
    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

class RhiProcessTestFixture final
{
  public:
    /// @brief 常時成功する既定 Log Sink と Process Fatal Handler を持つ Test Context を構築する
    RhiProcessTestFixture() noexcept : RhiProcessTestFixture(make_accepting_sinks())
    {
    }

    /// @brief 呼び出し側が用意した Log Sink と Process Fatal Handler を持つ Test Context を構築する
    explicit RhiProcessTestFixture(std::vector<std::unique_ptr<LogSink>> &&a_sinks) noexcept
        : m_logger(m_fatalHandler, std::move(a_sinks)), m_assertContext(m_logger, m_fatalHandler)
    {
    }

    /// @brief Fixture が所有する Logger と Fatal Handler を束ねた Assert Context を返す
    [[nodiscard]] AssertContext &assert_context() noexcept
    {
        return m_assertContext;
    }

  private:
    /// @brief 既定 Process Test 用の常時成功 Log Sink Collection を生成する
    [[nodiscard]] static std::vector<std::unique_ptr<LogSink>> make_accepting_sinks()
    {
        std::vector<std::unique_ptr<LogSink>> sinks;
        sinks.push_back(std::make_unique<AcceptingLogSink>());
        return sinks;
    }

    ProcessFatalHandler m_fatalHandler;
    Logger m_logger;
    AssertContext m_assertContext;
};
} // namespace cue::test
