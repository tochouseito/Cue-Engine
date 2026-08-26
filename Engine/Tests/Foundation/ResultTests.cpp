#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Result.h>

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
class TestEmergencyHandler final : public cue::FatalHandler
{
  public:
    /// @brief 通常 Fatal 診断後に Test Process を終了する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

template <typename T>
concept HasRvalueValueProbe = requires(T &&a_result) { std::move(a_result).try_value(); };

template <typename T>
concept HasRvalueErrorProbe = requires(T &&a_result) { std::move(a_result).try_error(); };

/// @brief ResultTests Test の Success が期待する契約を満たすか検証する
[[nodiscard]] bool test_success()
{
    cue::Result<int> result = cue::Result<int>::success(42);
    const int *value = result.try_value();
    return result.has_value() && static_cast<bool>(result) && value != nullptr && *value == 42 &&
           result.try_error() == nullptr;
}

/// @brief ResultTests Test の Failure が期待する契約を満たすか検証する
[[nodiscard]] bool test_failure(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue.Foundation.Test", 7);
    cue::NativeError nativeError = cue::NativeError::create(a_emergencyHandler, "Win32", 8);
    cue::Error error =
        cue::Error::create(a_emergencyHandler, std::move(code), "resource unavailable", std::move(nativeError));
    error.add_context(a_emergencyHandler, "test boundary");

    cue::Result<std::string> result = cue::Result<std::string>::failure(std::move(error));
    const cue::Error *storedError = result.try_error();

    return !result.has_value() && !static_cast<bool>(result) && result.try_value() == nullptr &&
           storedError != nullptr && storedError->code().domain() == "Cue.Foundation.Test" &&
           storedError->code().value() == 7 && storedError->summary() == "resource unavailable" &&
           storedError->contexts().size() == 1 && storedError->contexts()[0].message() == "test boundary" &&
           storedError->contexts()[0].location().line() != 0 &&
           storedError->contexts()[0].location().file_name().find("ResultTests.cpp") != std::string_view::npos &&
           storedError->try_native_error() != nullptr && storedError->try_native_error()->domain() == "Win32" &&
           storedError->try_native_error()->value() == 8;
}

/// @brief ResultTests Test の Reclassification が期待する契約を満たすか検証する
[[nodiscard]] bool test_reclassification(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode rootCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Root", 11);
    cue::Error rootError = cue::Error::create(a_emergencyHandler, std::move(rootCode), "root failure");
    rootError.add_context(a_emergencyHandler, "root context");

    cue::ErrorCode primaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Primary", 22);
    cue::Error primaryError =
        cue::Error::reclassify(a_emergencyHandler, std::move(primaryCode), "primary failure", std::move(rootError));

    return primaryError.code().domain() == "Cue.Primary" && primaryError.root_code().domain() == "Cue.Root" &&
           primaryError.causes().size() == 1 && primaryError.causes()[0].summary() == "root failure" &&
           primaryError.causes()[0].contexts().size() == 1;
}

/// @brief ResultTests Test の Native Reclassification が期待する契約を満たすか検証する
[[nodiscard]] bool test_native_reclassification(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode rootCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Root", 31);
    cue::NativeError rootNativeError = cue::NativeError::create(a_emergencyHandler, "D3D12", -1);
    cue::Error rootError =
        cue::Error::create(a_emergencyHandler, std::move(rootCode), "root failure", std::move(rootNativeError));
    rootError.add_context(a_emergencyHandler, "root context");

    cue::ErrorCode middleCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Middle", 32);
    cue::Error middleError =
        cue::Error::reclassify(a_emergencyHandler, std::move(middleCode), "middle failure", std::move(rootError));

    cue::ErrorCode primaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Primary", 33);
    cue::NativeError primaryNativeError = cue::NativeError::create(a_emergencyHandler, "Win32", 6);
    cue::Error primaryError = cue::Error::reclassify(a_emergencyHandler, std::move(primaryCode), "primary failure",
                                                     std::move(primaryNativeError), std::move(middleError));
    const cue::NativeError *storedPrimaryNativeError = primaryError.try_native_error();

    return primaryError.code().domain() == "Cue.Primary" && storedPrimaryNativeError != nullptr &&
           storedPrimaryNativeError->domain() == "Win32" && storedPrimaryNativeError->value() == 6 &&
           primaryError.causes().size() == 2 && primaryError.causes()[0].code().domain() == "Cue.Middle" &&
           primaryError.causes()[1].code().domain() == "Cue.Root" &&
           primaryError.causes()[1].try_native_error() != nullptr &&
           primaryError.causes()[1].try_native_error()->domain() == "D3D12" &&
           primaryError.causes()[1].try_native_error()->value() == -1 &&
           primaryError.causes()[1].contexts().size() == 1 && primaryError.root_code().domain() == "Cue.Root";
}

/// @brief ResultTests Test の Void Result が期待する契約を満たすか検証する
[[nodiscard]] bool test_void_result(TestEmergencyHandler &a_emergencyHandler)
{
    cue::Result<void> success = cue::Result<void>::success();

    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue.Void", 3);
    cue::Error error = cue::Error::create(a_emergencyHandler, std::move(code), "void failure");
    cue::Result<void> failure = cue::Result<void>::failure(std::move(error));

    return success.has_value() && success.try_error() == nullptr && !failure.has_value() &&
           failure.try_error() != nullptr;
}

/// @brief ResultTests Test の Move が期待する契約を満たすか検証する
[[nodiscard]] bool test_move()
{
    cue::Result<std::string> original = cue::Result<std::string>::success(std::string("moved value"));
    cue::Result<std::string> moved = std::move(original);
    const std::string *value = moved.try_value();
    return value != nullptr && *value == "moved value";
}

/// @brief Secondary Error の全診断情報と SourceLocation を規定順で保持することを検証する
[[nodiscard]] bool test_secondary_error_diagnostics(
    TestEmergencyHandler &a_emergencyHandler, const cue::AssertContext &a_assertContext)
{
    cue::ErrorCode primaryRootCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Primary.Root", 1);
    cue::Error primaryRoot = cue::Error::create(a_emergencyHandler, std::move(primaryRootCode), "primary root");
    cue::ErrorCode primaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Primary", 2);
    cue::NativeError primaryNative = cue::NativeError::create(a_emergencyHandler, "Win32", 3);
    cue::Error primary = cue::Error::reclassify(
        a_emergencyHandler, std::move(primaryCode), "primary summary", std::move(primaryNative),
        std::move(primaryRoot));
    primary.add_context(a_emergencyHandler, "primary context");

    cue::ErrorCode secondaryRootCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Secondary.Root", 10);
    cue::NativeError secondaryRootNative = cue::NativeError::create(a_emergencyHandler, "HRESULT", -1);
    cue::Error secondaryRoot = cue::Error::create(
        a_emergencyHandler, std::move(secondaryRootCode), "secondary root summary",
        std::move(secondaryRootNative));
    secondaryRoot.add_context(a_emergencyHandler, "secondary root context");
    const std::uint_least32_t secondaryRootContextLine = secondaryRoot.contexts()[0].location().line();

    cue::ErrorCode secondaryMiddleCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Secondary.Middle", 11);
    cue::Error secondaryMiddle = cue::Error::reclassify(
        a_emergencyHandler, std::move(secondaryMiddleCode), "secondary middle summary",
        std::move(secondaryRoot));
    secondaryMiddle.add_context(a_emergencyHandler, "secondary middle context");
    const std::uint_least32_t secondaryMiddleContextLine = secondaryMiddle.contexts()[0].location().line();

    cue::ErrorCode secondaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Secondary", 12);
    cue::NativeError secondaryNative = cue::NativeError::create(a_emergencyHandler, "Win32", 13);
    cue::Error secondary = cue::Error::reclassify(
        a_emergencyHandler, std::move(secondaryCode), "secondary summary", std::move(secondaryNative),
        std::move(secondaryMiddle));
    secondary.add_context(a_emergencyHandler, "secondary context");
    const std::uint_least32_t secondaryContextLine = secondary.contexts()[0].location().line();

    const std::uint_least32_t appendLine = __LINE__ + 1;
    primary.append_secondary_diagnostics(
        a_assertContext, secondary, "cleanup also failed", "Secondary Test Error");

    const std::span<const cue::ErrorContext> contexts = primary.contexts();
    const cue::NativeError *primaryStoredNative = primary.try_native_error();
    const bool primaryUnchanged = primary.code().domain() == "Cue.Primary" && primary.code().value() == 2 &&
                                  primary.summary() == "primary summary" && primaryStoredNative != nullptr &&
                                  primaryStoredNative->domain() == "Win32" && primaryStoredNative->value() == 3 &&
                                  primary.causes().size() == 1 && primary.root_code().domain() == "Cue.Primary.Root";

    if (!primaryUnchanged || contexts.size() != 15)
    {
        return false;
    }

    const bool messagesMatch = contexts[0].message() == "primary context" &&
                               contexts[1].message() == "cleanup also failed" &&
                               contexts[2].message() == "secondary summary" &&
                               contexts[3].message() == "Secondary Test Error Code=Cue.Secondary/12" &&
                               contexts[4].message() == "Secondary Test Error NativeError=Win32/13" &&
                               contexts[5].message() == "secondary context" &&
                               contexts[6].message() == "Secondary Test Error Cause" &&
                               contexts[7].message() == "secondary middle summary" &&
                               contexts[8].message() == "Secondary Test Error Cause Code=Cue.Secondary.Middle/11" &&
                               contexts[9].message() == "secondary middle context" &&
                               contexts[10].message() == "Secondary Test Error Cause" &&
                               contexts[11].message() == "secondary root summary" &&
                               contexts[12].message() == "Secondary Test Error Cause Code=Cue.Secondary.Root/10" &&
                               contexts[13].message() == "Secondary Test Error Cause NativeError=HRESULT/-1" &&
                               contexts[14].message() == "secondary root context";
    const bool generatedLocationsMatch = contexts[1].location().line() == appendLine &&
                                         contexts[2].location().line() == appendLine &&
                                         contexts[3].location().line() == appendLine &&
                                         contexts[4].location().line() == appendLine &&
                                         contexts[6].location().line() == appendLine &&
                                         contexts[7].location().line() == appendLine &&
                                         contexts[8].location().line() == appendLine &&
                                         contexts[10].location().line() == appendLine &&
                                         contexts[11].location().line() == appendLine &&
                                         contexts[12].location().line() == appendLine &&
                                         contexts[13].location().line() == appendLine;
    const bool preservedLocationsMatch = contexts[5].location().line() == secondaryContextLine &&
                                         contexts[9].location().line() == secondaryMiddleContextLine &&
                                         contexts[14].location().line() == secondaryRootContextLine;
    return messagesMatch && generatedLocationsMatch && preservedLocationsMatch;
}

/// @brief Secondary ErrorCause 単体を余分な Native Context なしで規定順に転記できることを検証する
[[nodiscard]] bool test_secondary_cause_diagnostics(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode causeCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Cause", 21);
    cue::Error cause = cue::Error::create(a_emergencyHandler, std::move(causeCode), "cause summary");
    cause.add_context(a_emergencyHandler, "cause context");
    const std::uint_least32_t causeContextLine = cause.contexts()[0].location().line();
    cue::ErrorCode secondaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Secondary", 22);
    cue::Error secondary = cue::Error::reclassify(
        a_emergencyHandler, std::move(secondaryCode), "secondary summary", std::move(cause));
    cue::ErrorCode primaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Primary", 23);
    cue::Error primary = cue::Error::create(a_emergencyHandler, std::move(primaryCode), "primary summary");

    const std::uint_least32_t appendLine = __LINE__ + 1;
    primary.append_secondary_diagnostics(
        a_emergencyHandler, secondary.causes()[0], "cause also failed", "Secondary Cause Test");

    const std::span<const cue::ErrorContext> contexts = primary.contexts();
    return contexts.size() == 4 && contexts[0].message() == "cause also failed" &&
           contexts[1].message() == "cause summary" &&
           contexts[2].message() == "Secondary Cause Test Code=Cue.Cause/21" &&
           contexts[3].message() == "cause context" && contexts[0].location().line() == appendLine &&
           contexts[1].location().line() == appendLine && contexts[2].location().line() == appendLine &&
           contexts[3].location().line() == causeContextLine;
}

/// @brief Primary Context を参照する Label が Context 再確保後も Secondary Cause 診断へ保持されることを検証する
[[nodiscard]] bool test_secondary_label_alias(
    TestEmergencyHandler &a_emergencyHandler, const cue::AssertContext &a_assertContext)
{
    cue::ErrorCode primaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Primary", 31);
    cue::Error primary = cue::Error::create(a_emergencyHandler, std::move(primaryCode), "primary summary");
    primary.add_context(a_emergencyHandler, "Aliased Label");
    const std::string_view aliasedLabel = primary.contexts()[0].message();

    cue::ErrorCode causeCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Secondary.Cause", 32);
    cue::Error cause = cue::Error::create(a_emergencyHandler, std::move(causeCode), "cause summary");
    cue::ErrorCode secondaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Secondary", 33);
    cue::Error secondary = cue::Error::reclassify(
        a_emergencyHandler, std::move(secondaryCode), "secondary summary", std::move(cause));

    primary.append_secondary_diagnostics(
        a_assertContext, secondary, "cleanup also failed", aliasedLabel);

    const std::span<const cue::ErrorContext> contexts = primary.contexts();
    return contexts.size() == 7 && contexts[3].message() == "Aliased Label Code=Cue.Secondary/33" &&
           contexts[4].message() == "Aliased Label Cause" &&
           contexts[6].message() == "Aliased Label Cause Code=Cue.Secondary.Cause/32";
}
} // namespace

static_assert(!std::is_copy_constructible_v<cue::Error>);
static_assert(!std::is_copy_constructible_v<cue::Result<int>>);
static_assert(std::is_nothrow_move_constructible_v<cue::Error>);
static_assert(std::is_nothrow_move_constructible_v<cue::Result<int>>);
static_assert(!HasRvalueValueProbe<cue::Result<int>>);
static_assert(!HasRvalueErrorProbe<cue::Result<int>>);

/// @brief Result の成功・失敗・Move 所有権契約を実行時に検証して終了 Code を返す
int main()
{
    TestEmergencyHandler emergencyHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(emergencyHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, emergencyHandler);

    if (!test_success())
    {
        return 1;
    }

    if (!test_failure(emergencyHandler))
    {
        return 2;
    }

    if (!test_reclassification(emergencyHandler))
    {
        return 3;
    }

    if (!test_void_result(emergencyHandler))
    {
        return 4;
    }

    if (!test_native_reclassification(emergencyHandler))
    {
        return 5;
    }

    if (!test_move())
    {
        return 6;
    }

    if (!test_secondary_error_diagnostics(emergencyHandler, assertContext))
    {
        return 7;
    }

    if (!test_secondary_cause_diagnostics(emergencyHandler))
    {
        return 8;
    }

    if (!test_secondary_label_alias(emergencyHandler, assertContext))
    {
        return 9;
    }

    return 0;
}
