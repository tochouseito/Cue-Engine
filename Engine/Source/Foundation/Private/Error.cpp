#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Assert.h>

#include <cstdlib>
#include <utility>

namespace
{
/// @brief Secondary Error の Code と任意 Native Error を転記前に所有する診断文字列
struct ErrorIdentityContexts final
{
    std::string code;
    std::optional<std::string> nativeError;
};

/// @brief 通常診断を継続できない失敗を Emergency Handler へ委譲して Process を終了する
[[noreturn]] void terminate_emergency(cue::EmergencyHandler &a_emergencyHandler, std::string_view a_message) noexcept
{
    a_emergencyHandler.terminate(a_message);
    // Source 上でも終了経路を明示するが、Handler が復帰した時点で `[[noreturn]]` 契約違反となる
    std::abort();
}

/// @brief Error の Code と任意 Native Error を指定 Label の診断文字列へ変換する
[[nodiscard]] ErrorIdentityContexts make_error_identity_contexts(
    std::string_view a_label, const cue::ErrorCode &a_code,
    const cue::NativeError *a_nativeError)
{
    std::string codeContext(a_label);
    codeContext.append(" Code=");
    codeContext.append(a_code.domain());
    codeContext.push_back('/');
    codeContext.append(std::to_string(a_code.value()));
    std::optional<std::string> nativeErrorContext;

    if (a_nativeError != nullptr)
    {
        std::string nativeContext(a_label);
        nativeContext.append(" NativeError=");
        nativeContext.append(a_nativeError->domain());
        nativeContext.push_back('/');
        nativeContext.append(std::to_string(a_nativeError->value()));
        nativeErrorContext.emplace(std::move(nativeContext));
    }

    return {std::move(codeContext), std::move(nativeErrorContext)};
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

/// @brief Secondary Error の診断を Primary Error の Context へ転記する
void Error::append_secondary_diagnostics(
    const AssertContext &a_assertContext, const Error &a_secondaryError,
    std::string_view a_context, std::string_view a_label,
    std::source_location a_location) noexcept
{
    CUE_ASSERT(a_assertContext, this != &a_secondaryError,
               "Secondary Error must not alias the Primary Error");

    if (this == &a_secondaryError)
    {
        return;
    }

    EmergencyHandler &emergencyHandler = a_assertContext.fatal_handler();

    try
    {
        const ErrorCode &secondaryCode = a_secondaryError.code();
        const NativeError *secondaryNativeError = a_secondaryError.try_native_error();
        const ErrorIdentityContexts identity = make_error_identity_contexts(
            a_label, secondaryCode, secondaryNativeError);

        add_context(emergencyHandler, a_context, a_location);
        add_context(emergencyHandler, a_secondaryError.summary(), a_location);
        add_context(emergencyHandler, identity.code, a_location);

        if (identity.nativeError)
        {
            add_context(emergencyHandler, *identity.nativeError, a_location);
        }

        for (const ErrorContext &context : a_secondaryError.contexts())
        {
            append_preserved_context(emergencyHandler, context);
        }

        for (const ErrorCause &cause : a_secondaryError.causes())
        {
            std::string causeLabel(a_label);
            causeLabel.append(" Cause");
            const ErrorIdentityContexts causeIdentity = make_error_identity_contexts(
                causeLabel, cause.code(), cause.try_native_error());
            add_context(emergencyHandler, causeLabel, a_location);
            add_context(emergencyHandler, cause.summary(), a_location);
            add_context(emergencyHandler, causeIdentity.code, a_location);

            if (causeIdentity.nativeError)
            {
                add_context(emergencyHandler, *causeIdentity.nativeError, a_location);
            }

            for (const ErrorContext &context : cause.contexts())
            {
                append_preserved_context(emergencyHandler, context);
            }
        }
    }
    catch (...)
    {
        terminate_emergency(emergencyHandler, "Secondary Error diagnostic composition failed");
    }
}

/// @brief Secondary Cause の診断を Primary Error の Context へ転記する
void Error::append_secondary_diagnostics(
    EmergencyHandler &a_emergencyHandler, const ErrorCause &a_secondaryCause,
    std::string_view a_context, std::string_view a_label,
    std::source_location a_location) noexcept
{
    try
    {
        const ErrorIdentityContexts identity = make_error_identity_contexts(
            a_label, a_secondaryCause.code(), a_secondaryCause.try_native_error());
        add_context(a_emergencyHandler, a_context, a_location);
        add_context(a_emergencyHandler, a_secondaryCause.summary(), a_location);
        add_context(a_emergencyHandler, identity.code, a_location);

        if (identity.nativeError)
        {
            add_context(a_emergencyHandler, *identity.nativeError, a_location);
        }

        for (const ErrorContext &context : a_secondaryCause.contexts())
        {
            append_preserved_context(a_emergencyHandler, context);
        }
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Secondary Cause diagnostic composition failed");
    }
}

/// @brief 転記元 Error Context の Message と SourceLocation を変更せず追加する
void Error::append_preserved_context(EmergencyHandler &a_emergencyHandler,
                                     const ErrorContext &a_context) noexcept
{
    try
    {
        ErrorContext context(std::string(a_context.message()), a_context.location());
        m_contexts.push_back(std::move(context));
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Preserved Error context creation failed");
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
