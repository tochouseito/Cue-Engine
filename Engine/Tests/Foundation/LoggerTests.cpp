#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Log.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
class TestEmergencyHandler final : public cue::EmergencyHandler
{
  public:
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }
};

struct RecordSnapshot
{
    cue::LogLevel level;
    std::string message;
    std::string errorDomain;
    std::int64_t errorValue;
};

class RecordingSink final : public cue::LogSink
{
  public:
    explicit RecordingSink(bool a_shouldFail = false) noexcept : m_shouldFail(a_shouldFail)
    {
    }

    [[nodiscard]] bool write(const cue::LogRecord &a_record) override
    {
        RecordSnapshot snapshot{a_record.level(), std::string(a_record.message()), std::string(), 0};
        if (const cue::Error *error = a_record.try_error())
        {
            snapshot.errorDomain = error->code().domain();
            snapshot.errorValue = error->code().value();
        }
        records.push_back(std::move(snapshot));
        return !m_shouldFail;
    }

    [[nodiscard]] bool flush() override
    {
        ++flushCount;
        return !m_shouldFail;
    }

    std::vector<RecordSnapshot> records;
    int flushCount = 0;

  private:
    bool m_shouldFail;
};

[[nodiscard]] bool test_records_and_error(TestEmergencyHandler &a_emergencyHandler)
{
    auto firstSink = std::make_unique<RecordingSink>();
    auto secondSink = std::make_unique<RecordingSink>();
    RecordingSink *first = firstSink.get();
    RecordingSink *second = secondSink.get();

    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::move(firstSink));
    sinks.push_back(std::move(secondSink));
    cue::Logger logger(a_emergencyHandler, std::move(sinks));

    if (logger.log(cue::LogLevel::Info, "started") != cue::LogResult::Success)
    {
        return false;
    }

    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue.Logger", 9);
    cue::Error error = cue::Error::create(a_emergencyHandler, std::move(code), "log error");
    if (logger.log(cue::LogLevel::Error, "failed", std::move(error)) != cue::LogResult::Success)
    {
        return false;
    }
    if (logger.flush() != cue::LogResult::Success)
    {
        return false;
    }

    return first->records.size() == 2 && second->records.size() == 2 && first->records[0].message == "started" &&
           first->records[1].errorDomain == "Cue.Logger" && first->records[1].errorValue == 9 &&
           first->flushCount == 1 && second->flushCount == 1;
}

[[nodiscard]] bool test_sink_failure_continues(TestEmergencyHandler &a_emergencyHandler)
{
    auto failingSink = std::make_unique<RecordingSink>(true);
    auto recordingSink = std::make_unique<RecordingSink>();
    RecordingSink *recorder = recordingSink.get();

    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::move(failingSink));
    sinks.push_back(std::move(recordingSink));
    cue::Logger logger(a_emergencyHandler, std::move(sinks));

    return logger.log(cue::LogLevel::Warning, "warning") == cue::LogResult::SinkFailure &&
           recorder->records.size() == 1;
}

[[nodiscard]] bool test_multiple_threads(TestEmergencyHandler &a_emergencyHandler)
{
    auto recordingSink = std::make_unique<RecordingSink>();
    RecordingSink *recorder = recordingSink.get();
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::move(recordingSink));
    cue::Logger logger(a_emergencyHandler, std::move(sinks));

    constexpr int k_threadCount = 4;
    constexpr int k_recordsPerThread = 50;
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < k_threadCount; ++threadIndex)
    {
        threads.emplace_back(
            [&logger]()
            {
                for (int recordIndex = 0; recordIndex < k_recordsPerThread; ++recordIndex)
                {
                    if (logger.log(cue::LogLevel::Debug, "thread record") != cue::LogResult::Success)
                    {
                        std::_Exit(79);
                    }
                }
            });
    }
    for (std::thread &thread : threads)
    {
        thread.join();
    }

    return recorder->records.size() == k_threadCount * k_recordsPerThread;
}

[[nodiscard]] bool test_console_sink(TestEmergencyHandler &a_emergencyHandler)
{
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<cue::ConsoleLogSink>());
    cue::Logger logger(a_emergencyHandler, std::move(sinks));
    return logger.log(cue::LogLevel::Info, "console sink probe") == cue::LogResult::Success &&
           logger.flush() == cue::LogResult::Success;
}
} // namespace

int main()
{
    TestEmergencyHandler emergencyHandler;
    if (!test_records_and_error(emergencyHandler))
    {
        return 1;
    }
    if (!test_sink_failure_continues(emergencyHandler))
    {
        return 2;
    }
    if (!test_multiple_threads(emergencyHandler))
    {
        return 3;
    }
    if (!test_console_sink(emergencyHandler))
    {
        return 4;
    }
    return 0;
}
