#pragma once

#include <Cue/Foundation/Error.h>

#include <d3d12.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;

namespace d3d12_private
{
/// @brief D3D12 Module Domainと識別値から内部Error Codeを生成する
[[nodiscard]] ErrorCode make_code(const AssertContext &a_context, std::int64_t a_value) noexcept;

/// @brief D3D12 Module Domainの失敗をNative情報を持たない診断Errorとして生成する
[[nodiscard]] Error make_error(const AssertContext &a_context, std::int64_t a_code,
                               std::string_view a_summary) noexcept;

/// @brief D3D12 HRESULT失敗をD3D12 Native Domain付きの診断Errorへ変換する
[[nodiscard]] Error make_native_error(const AssertContext &a_context, std::int64_t a_code,
                                      std::string_view a_summary, HRESULT a_nativeCode) noexcept;

/// @brief 任意のNative DomainとCodeをD3D12 Moduleの診断Errorへ変換する
[[nodiscard]] Error make_native_error(const AssertContext &a_context, std::int64_t a_code,
                                      std::string_view a_summary, std::string_view a_nativeDomain,
                                      std::int64_t a_nativeCode) noexcept;

/// @brief DXGI HRESULT失敗を既定のDXGI Native Domainまたは指定Domain付きの診断Errorへ変換する
[[nodiscard]] Error make_dxgi_error(const AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary, HRESULT a_nativeCode,
                                    std::string_view a_nativeDomain = "DXGI") noexcept;

/// @brief Win32 API失敗をWin32 Native Domain付きのD3D12診断Errorへ変換する
[[nodiscard]] Error make_win32_error(const AssertContext &a_context, std::int64_t a_code,
                                     std::string_view a_summary, DWORD a_nativeCode) noexcept;

/// @brief D3D12 Objectへ診断名を設定し、Native HRESULTをそのまま呼び出し元へ返す
HRESULT set_object_name(ID3D12Object *a_object, LPCWSTR a_name) noexcept;
} // namespace d3d12_private
} // namespace cue
