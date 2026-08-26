#include <Cue/Foundation/Error.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string_view>
#include <utility>

namespace
{
constexpr int k_emergencyExitCode = 73;
std::atomic<bool> shouldFailNextAllocation = false;

/// @brief Allocation 経路の設定に従って Memory を確保し、確保結果を返す
[[nodiscard]] void *allocate(std::size_t a_size)
{
    if (shouldFailNextAllocation.exchange(false))
    {
        throw std::bad_alloc();
    }

    if (void *memory = std::malloc(a_size))
    {
        return memory;
    }

    throw std::bad_alloc();
}

class TestEmergencyHandler final : public cue::EmergencyHandler
{
  public:
    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(k_emergencyExitCode);
    }
};

/// @brief EmergencyFailureTests Test の Create Error Code が期待する契約を満たすか検証する
[[nodiscard]] int test_create_error_code(TestEmergencyHandler &a_emergencyHandler)
{
    shouldFailNextAllocation = true;
    [[maybe_unused]] cue::ErrorCode code =
        cue::ErrorCode::create(a_emergencyHandler, "Cue.Foundation.Emergency.ErrorCode.Domain.RequiresAllocation", 1);
    return 1;
}

/// @brief EmergencyFailureTests Test の Create Error が期待する契約を満たすか検証する
[[nodiscard]] int test_create_error(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue", 2);
    shouldFailNextAllocation = true;
    [[maybe_unused]] cue::Error error = cue::Error::create(
        a_emergencyHandler, std::move(code), "Error summary that requires an allocation in the test process");
    return 2;
}

/// @brief EmergencyFailureTests Test の Reclassify が期待する契約を満たすか検証する
[[nodiscard]] int test_reclassify(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode causeCode = cue::ErrorCode::create(a_emergencyHandler, "Cue", 3);
    cue::Error cause = cue::Error::create(a_emergencyHandler, std::move(causeCode), "cause");
    cue::ErrorCode primaryCode = cue::ErrorCode::create(a_emergencyHandler, "Cue", 4);
    shouldFailNextAllocation = true;
    [[maybe_unused]] cue::Error error =
        cue::Error::reclassify(a_emergencyHandler, std::move(primaryCode),
                               "Reclassified error summary that requires an allocation", std::move(cause));
    return 3;
}

/// @brief Context 追加時の Allocation 失敗が Emergency Handler による Process 終了へ移行することを検証する
[[nodiscard]] int test_add_context(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue", 5);
    cue::Error error = cue::Error::create(a_emergencyHandler, std::move(code), "error");
    shouldFailNextAllocation = true;
    error.add_context(a_emergencyHandler, "Error context that requires an allocation in the test process");
    return 4;
}
} // namespace

/// @brief EmergencyFailureTests Test で Allocation 経路を制御するための Memory を確保する
void *operator new(std::size_t a_size)
{
    return allocate(a_size);
}

/// @brief EmergencyFailureTests Test で Allocation 経路を制御するための Memory を確保する
void *operator new[](std::size_t a_size)
{
    return allocate(a_size);
}

/// @brief EmergencyFailureTests Test で Allocation 経路を制御するために確保した Memory を解放する
void operator delete(void *a_memory) noexcept
{
    std::free(a_memory);
}

/// @brief EmergencyFailureTests Test で Allocation 経路を制御するために確保した Memory を解放する
void operator delete[](void *a_memory) noexcept
{
    std::free(a_memory);
}

/// @brief EmergencyFailureTests Test で Allocation 経路を制御するために確保した Memory を解放する
void operator delete(void *a_memory, std::size_t) noexcept
{
    std::free(a_memory);
}

/// @brief EmergencyFailureTests Test で Allocation 経路を制御するために確保した Memory を解放する
void operator delete[](void *a_memory, std::size_t) noexcept
{
    std::free(a_memory);
}

/// @brief 指定 Scenario の Allocation 失敗を再現し、Emergency 終了経路を Process 単位で検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 10;
    }

    TestEmergencyHandler emergencyHandler;
    const std::string_view mode = a_arguments[1];

    if (mode == "CreateErrorCode")
    {
        return test_create_error_code(emergencyHandler);
    }
    if (mode == "CreateError")
    {
        return test_create_error(emergencyHandler);
    }
    if (mode == "Reclassify")
    {
        return test_reclassify(emergencyHandler);
    }
    if (mode == "AddContext")
    {
        return test_add_context(emergencyHandler);
    }

    return 11;
}
