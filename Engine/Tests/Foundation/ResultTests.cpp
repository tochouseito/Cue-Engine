#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Result.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
class TestEmergencyHandler final : public cue::EmergencyHandler
{
  public:
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

    return 0;
}
