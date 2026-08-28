// D3D12 Device を作成できる DXGI Adapter を選び、上位 Module へ Capability を報告する内部契約
// Hardware と Software の選択規則を一箇所に集め、実行環境による選択差を診断可能にする

#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/GraphicsBackend.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cue
{
class AssertContext;
struct D3d12DiagnosticsStatus;

struct D3d12AdapterCandidateFacts final
{
    bool isSoftware;
    bool supportsRequiredFeatureLevel;
};

struct D3d12AdapterReport final
{
    std::string adapterName;
    std::uint64_t dedicatedVideoMemoryBytes;
    std::uint32_t vendorId;
    std::uint32_t deviceId;
    GraphicsAdapterKind adapterKind;
};

// 選択結果を D3D12 Native Object と Platform 非依存の報告値へ分けて保持する
struct D3d12AdapterSelection final
{
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
    D3d12AdapterReport report;
    D3D_FEATURE_LEVEL featureLevel;
};

// Native 生成関数を注入可能にし、OS や GPU に依存する失敗経路を検証可能にする
using D3d12FactoryCreator = HRESULT(WINAPI *)(UINT, REFIID, void **);

/// @brief D3D12 Adapter 選択の Supported Hardware Candidate 条件を判定して返す
[[nodiscard]] bool is_supported_hardware_candidate(D3d12AdapterCandidateFacts a_candidate) noexcept;

/// @brief Debug DXGI Factory 生成失敗を通常 Factory で再試行可能か判定する
[[nodiscard]] bool should_retry_without_debug_factory(HRESULT a_result, bool a_wasDebugRequested) noexcept;

/// @brief D3D12 Adapter 選択で使用する D3D12 Factory を生成し、呼び出し元へ返す
[[nodiscard]] Result<Microsoft::WRL::ComPtr<IDXGIFactory6>> create_d3d12_factory(
    UINT a_flags, D3d12FactoryCreator a_creator, const AssertContext &a_assertContext) noexcept;

/// @brief D3D12 Adapter 選択で使用する D3D12 Capability Report を生成し、呼び出し元へ返す
[[nodiscard]] Result<CapabilityReport> make_d3d12_capability_report(
    const D3d12AdapterReport &a_report, const AssertContext &a_assertContext) noexcept;

/// @brief DXGI Adapter の UTF-16 名を既存の D3D12 Error 契約を保って UTF-8 へ変換する
[[nodiscard]] Result<std::string> convert_d3d12_adapter_name(
    std::wstring_view a_name, const AssertContext &a_assertContext) noexcept;

/// @brief D3D12 Adapter 選択の D3D12 Adapter を条件に従って選択または取得し、診断可能な結果を返す
[[nodiscard]] Result<D3d12AdapterSelection> select_d3d12_adapter(
    D3d12AdapterPolicy a_policy, const D3d12DiagnosticsStatus &a_diagnostics,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
