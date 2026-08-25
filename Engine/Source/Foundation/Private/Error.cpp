#include <Cue/Foundation/Error.h>

#include <cstdlib>
#include <utility>

namespace
{
[[noreturn]] void terminate_emergency(cue::EmergencyHandler &a_emergencyHandler, std::string_view a_message) noexcept
{
    a_emergencyHandler.terminate(a_message);
    // 外部 Handler の実装違反があっても、構築に失敗した不完全な Error で実行を継続させない
    std::abort();
}
} // namespace

namespace cue
{
ErrorCode ErrorCode::create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                            std::int64_t a_value) noexcept
{
    // 公開 API の noexcept 契約を守り、診断 Value を作れない場合は独立した Emergency 経路で終了する
    try
    {
        return ErrorCode(std::string(a_domain), a_value);
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "ErrorCode creation failed");
    }
}

std::string_view ErrorCode::domain() const noexcept
{
    return m_domain;
}

std::int64_t ErrorCode::value() const noexcept
{
    return m_value;
}

ErrorCode::ErrorCode(std::string &&a_domain, std::int64_t a_value) noexcept
    : m_domain(std::move(a_domain)), m_value(a_value)
{
}

NativeError NativeError::create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                std::int64_t a_value) noexcept
{
    // Native Domain 名の Allocation 失敗も Error として再帰的に表現せず、Emergency 経路へ集約する
    try
    {
        return NativeError(std::string(a_domain), a_value);
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "NativeError creation failed");
    }
}

std::string_view NativeError::domain() const noexcept
{
    return m_domain;
}

std::int64_t NativeError::value() const noexcept
{
    return m_value;
}

NativeError::NativeError(std::string &&a_domain, std::int64_t a_value) noexcept
    : m_domain(std::move(a_domain)), m_value(a_value)
{
}

ErrorContext::ErrorContext(std::string &&a_message, SourceLocation a_location) noexcept
    : m_message(std::move(a_message)), m_location(a_location)
{
}

std::string_view ErrorContext::message() const noexcept
{
    return m_message;
}

const SourceLocation &ErrorContext::location() const noexcept
{
    return m_location;
}

ErrorCause::ErrorCause(ErrorCode &&a_code, std::string &&a_summary, std::vector<ErrorContext> &&a_contexts,
                       std::optional<NativeError> &&a_nativeError) noexcept
    : m_code(std::move(a_code)), m_summary(std::move(a_summary)), m_contexts(std::move(a_contexts)),
      m_nativeError(std::move(a_nativeError))
{
}

const ErrorCode &ErrorCause::code() const noexcept
{
    return m_code;
}

std::string_view ErrorCause::summary() const noexcept
{
    return m_summary;
}

std::span<const ErrorContext> ErrorCause::contexts() const noexcept
{
    return m_contexts;
}

const NativeError *ErrorCause::try_native_error() const noexcept
{
    return m_nativeError ? &m_nativeError.value() : nullptr;
}

Error Error::create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code, std::string_view a_summary) noexcept
{
    // Error 生成自体の失敗を別の Error で包む再帰を避け、noexcept 境界を維持する
    try
    {
        return Error(std::move(a_code), std::string(a_summary), std::nullopt);
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Error creation failed");
    }
}

Error Error::create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code, std::string_view a_summary,
                    NativeError &&a_nativeError) noexcept
{
    try
    {
        return Error(std::move(a_code), std::string(a_summary), std::optional<NativeError>(std::move(a_nativeError)));
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Error creation failed");
    }
}

Error Error::reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code, std::string_view a_summary,
                        Error &&a_cause) noexcept
{
    return reclassify_impl(a_emergencyHandler, std::move(a_code), a_summary, std::nullopt, std::move(a_cause));
}

Error Error::reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code, std::string_view a_summary,
                        NativeError &&a_nativeError, Error &&a_cause) noexcept
{
    return reclassify_impl(a_emergencyHandler, std::move(a_code), a_summary,
                           std::optional<NativeError>(std::move(a_nativeError)), std::move(a_cause));
}

Error Error::reclassify_impl(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code, std::string_view a_summary,
                             std::optional<NativeError> &&a_nativeError, Error &&a_cause) noexcept
{
    try
    {
        Error result(std::move(a_code), std::string(a_summary), std::move(a_nativeError));

        // 先頭を Immediate Cause、末尾を Root Cause とする平坦な順序を再分類後も維持する
        result.m_causes.reserve(a_cause.m_causes.size() + 1);
        ErrorCause causeFrame(std::move(a_cause.m_code), std::move(a_cause.m_summary), std::move(a_cause.m_contexts),
                              std::move(a_cause.m_nativeError));
        result.m_causes.push_back(std::move(causeFrame));

        for (ErrorCause &existingCause : a_cause.m_causes)
        {
            result.m_causes.push_back(std::move(existingCause));
        }

        return result;
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Error reclassification failed");
    }
}

void Error::add_context(EmergencyHandler &a_emergencyHandler, std::string_view a_message,
                        std::source_location a_location) noexcept
{
    // 分類を変えずに伝播経路だけを追加し、各 Module が同じ Error を段階的に説明できるようにする
    try
    {
        ErrorContext context(std::string(a_message), SourceLocation::from(a_location));
        m_contexts.push_back(std::move(context));
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Error context creation failed");
    }
}

const ErrorCode &Error::code() const noexcept
{
    return m_code;
}

std::string_view Error::summary() const noexcept
{
    return m_summary;
}

std::span<const ErrorContext> Error::contexts() const noexcept
{
    return m_contexts;
}

const NativeError *Error::try_native_error() const noexcept
{
    return m_nativeError ? &m_nativeError.value() : nullptr;
}

std::span<const ErrorCause> Error::causes() const noexcept
{
    return m_causes;
}

const ErrorCode &Error::root_code() const noexcept
{
    if (m_causes.empty())
    {
        return m_code;
    }

    return m_causes.back().code();
}

Error::Error(ErrorCode &&a_code, std::string &&a_summary, std::optional<NativeError> &&a_nativeError) noexcept
    : m_code(std::move(a_code)), m_summary(std::move(a_summary)), m_nativeError(std::move(a_nativeError))
{
}
} // namespace cue
