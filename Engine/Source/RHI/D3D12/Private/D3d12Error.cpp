#include "D3d12Error.h"

#include <Cue/Foundation/Assert.h>

#include <utility>

namespace cue::d3d12_private
{
/// @brief D3D12内部Errorが共有するModule Domainと識別値をError Codeへ格納する
ErrorCode make_code(const AssertContext &a_context, std::int64_t a_value) noexcept
{
    return ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_value);
}

/// @brief Native APIに由来しないD3D12内部失敗を共通DomainのErrorへ変換する
Error make_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary) noexcept
{
    return Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary);
}

/// @brief HRESULTをD3D12 Native Domainとして保持するError生成Overloadへ転送する
Error make_native_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary,
                        HRESULT a_nativeCode) noexcept
{
    return make_native_error(a_context, a_code, a_summary, "D3D12", static_cast<std::int64_t>(a_nativeCode));
}

/// @brief 指定Native DomainとCodeを失わずD3D12 Module Errorへ格納する
Error make_native_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary,
                        std::string_view a_nativeDomain, std::int64_t a_nativeCode) noexcept
{
    NativeError nativeError = NativeError::create(a_context.fatal_handler(), a_nativeDomain, a_nativeCode);
    return Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary, std::move(nativeError));
}

/// @brief DXGI HRESULTと指定Domainを共通Native Error生成処理へ転送する
Error make_dxgi_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary,
                      HRESULT a_nativeCode, std::string_view a_nativeDomain) noexcept
{
    return make_native_error(a_context, a_code, a_summary, a_nativeDomain,
                             static_cast<std::int64_t>(a_nativeCode));
}

/// @brief Win32 Error CodeをWin32 Native Domainとして共通Native Error生成処理へ転送する
Error make_win32_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary,
                       DWORD a_nativeCode) noexcept
{
    return make_native_error(a_context, a_code, a_summary, "Win32", static_cast<std::int64_t>(a_nativeCode));
}

/// @brief 指定D3D12 Objectへ診断名を設定するNative呼び出しを一箇所から提供する
HRESULT set_object_name(ID3D12Object *a_object, LPCWSTR a_name) noexcept
{
    return a_object->SetName(a_name);
}
} // namespace cue::d3d12_private
