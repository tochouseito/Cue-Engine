#pragma once

#include <Cue/Foundation/EmergencyHandler.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Log.h>

#include <source_location>
#include <string_view>

namespace cue
{
/// @brief 通常 Fatal と Emergency 終了を提供する非所有終了境界
///
/// Log 出力可能な通常 Fatal と、Logger を利用できない Emergency を同じ Process 終了 Policy へ集約する
class FatalHandler : public EmergencyHandler
{
  public:
    using EmergencyHandler::terminate;

    /// @brief EmergencyHandler 契約を Fatal 終了処理へ特化するための基底状態を構築する
    FatalHandler() = default;
    /// @brief 基底 Pointer を介して派生 Fatal Handler を正しく破棄できるようにする
    ~FatalHandler() override = default;

    /// @brief FatalHandler の一意所有を保つため Copy 構築を禁止する
    FatalHandler(const FatalHandler &) = delete;
    /// @brief FatalHandler の一意所有を保つため Copy 代入を禁止する
    FatalHandler &operator=(const FatalHandler &) = delete;
    /// @brief FatalHandler の所有状態を移動させないため Move 構築を禁止する
    FatalHandler(FatalHandler &&) = delete;
    /// @brief FatalHandler の所有状態を移動させないため Move 代入を禁止する
    FatalHandler &operator=(FatalHandler &&) = delete;

    /// @brief 通常 Fatal 診断後に Process を終了する
    [[noreturn]] virtual void terminate() noexcept = 0;
};

/// @brief 回復不能な状態から実行を継続させないため `std::abort` で終了する Production 既定 Handler
class AbortFatalHandler final : public FatalHandler
{
  public:
    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate() noexcept override;
    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate(std::string_view a_message) noexcept override;
};

/// @brief Fatal Record を出力後に Process を終了する
///
/// 診断を可能な限り Flush してから終了し、Logger 競合時は待機せず Emergency 経路へ切り替える
[[noreturn]] void report_fatal(Logger &a_logger, FatalHandler &a_fatalHandler, std::string_view a_message,
                               std::source_location a_location = std::source_location::current()) noexcept;

/// @brief Error の詳細を Fatal Record へ移してから同じ終了保証を適用する
[[noreturn]] void report_fatal(Logger &a_logger, FatalHandler &a_fatalHandler, std::string_view a_message,
                               Error &&a_error,
                               std::source_location a_location = std::source_location::current()) noexcept;
} // namespace cue
