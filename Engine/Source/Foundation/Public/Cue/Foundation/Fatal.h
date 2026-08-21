#pragma once

#include <Cue/Foundation/EmergencyHandler.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Log.h>

#include <source_location>
#include <string_view>

namespace cue
{
/** @brief 通常FatalとEmergency終了を提供する非所有終了境界 */
class FatalHandler : public EmergencyHandler
{
  public:
    using EmergencyHandler::terminate;

    FatalHandler() = default;
    ~FatalHandler() override = default;

    FatalHandler(const FatalHandler &) = delete;
    FatalHandler &operator=(const FatalHandler &) = delete;
    FatalHandler(FatalHandler &&) = delete;
    FatalHandler &operator=(FatalHandler &&) = delete;

    /** @brief 通常Fatal診断後にProcessを終了する */
    [[noreturn]] virtual void terminate() noexcept = 0;
};

/** @brief `std::abort`で終了するProduction既定Handler */
class AbortFatalHandler final : public FatalHandler
{
  public:
    [[noreturn]] void terminate() noexcept override;
    [[noreturn]] void terminate(std::string_view a_message) noexcept override;
};

/** @brief Fatal Recordを出力後にProcessを終了する */
[[noreturn]] void report_fatal(Logger &a_logger, FatalHandler &a_fatalHandler, std::string_view a_message,
                               std::source_location a_location = std::source_location::current()) noexcept;

/** @brief Error付きFatal Recordを出力後にProcessを終了する */
[[noreturn]] void report_fatal(Logger &a_logger, FatalHandler &a_fatalHandler, std::string_view a_message,
                               Error &&a_error,
                               std::source_location a_location = std::source_location::current()) noexcept;
} // namespace cue
