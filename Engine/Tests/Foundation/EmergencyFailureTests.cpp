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
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(k_emergencyExitCode);
    }
};

[[nodiscard]] int test_create_error_code(TestEmergencyHandler &a_emergencyHandler)
{
    shouldFailNextAllocation = true;
    [[maybe_unused]] cue::ErrorCode code =
        cue::ErrorCode::create(a_emergencyHandler, "Cue.Foundation.Emergency.ErrorCode.Domain.RequiresAllocation", 1);
    return 1;
}

[[nodiscard]] int test_create_error(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue", 2);
    shouldFailNextAllocation = true;
    [[maybe_unused]] cue::Error error = cue::Error::create(
        a_emergencyHandler, std::move(code), "Error summary that requires an allocation in the test process");
    return 2;
}

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

[[nodiscard]] int test_add_context(TestEmergencyHandler &a_emergencyHandler)
{
    cue::ErrorCode code = cue::ErrorCode::create(a_emergencyHandler, "Cue", 5);
    cue::Error error = cue::Error::create(a_emergencyHandler, std::move(code), "error");
    shouldFailNextAllocation = true;
    error.add_context(a_emergencyHandler, "Error context that requires an allocation in the test process");
    return 4;
}
} // namespace

void *operator new(std::size_t a_size)
{
    return allocate(a_size);
}

void *operator new[](std::size_t a_size)
{
    return allocate(a_size);
}

void operator delete(void *a_memory) noexcept
{
    std::free(a_memory);
}

void operator delete[](void *a_memory) noexcept
{
    std::free(a_memory);
}

void operator delete(void *a_memory, std::size_t) noexcept
{
    std::free(a_memory);
}

void operator delete[](void *a_memory, std::size_t) noexcept
{
    std::free(a_memory);
}

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
