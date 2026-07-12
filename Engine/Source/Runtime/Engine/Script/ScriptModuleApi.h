#pragma once

/// **********************************************************************
/// GameScript module と Engine 間の最小 ABI を定義する
/// **********************************************************************

// === C++ includes ===
#include <cstdint>

#if defined(_WIN32)
// Engine と GameScript DLL で呼び出し規約を固定し、関数境界のスタック破壊を防ぐ
#define CUE_SCRIPT_CALL __cdecl
#else
#define CUE_SCRIPT_CALL
#endif

namespace Cue::Script
{
    /// @brief Engine と GameScript が互換であることを確認する ABI 世代
    inline constexpr uint32_t k_scriptModuleAbiVersion = 1u;

    /// @brief Script DLL が未生成または破棄済みであることを表す instance handle
    inline constexpr uint64_t k_invalidScriptInstanceHandle = 0u;

    /// @brief Script DLL が Engine へ返す失敗理由
    ///
    /// ABI 境界では例外を越境させず、Engine が失敗した操作を判断できる値を返却
    enum class ScriptResult : uint32_t
    {
        /// @brief 操作完了
        Ok = 0u,

        /// @brief 必須の入力値または出力先が不正
        InvalidArgument,

        /// @brief 指定された Script class または instance が未発見
        NotFound,

        /// @brief module のロード状態などが操作の前提を未充足
        InvalidState,

        /// @brief GameScript 側で詳細を公開できない失敗
        InternalError,
    };

    /// @brief Engine が Script instance の生成時に渡す識別情報
    struct ScriptInstanceCreateInfo final
    {
        /// @brief instance を対応付ける runtime World 上の Entity ID
        uint32_t entityId = 0u;

        /// @brief GameScript が生成する登録済み Script class 名
        ///
        /// 呼び出し中だけ有効な UTF-8 文字列として扱い、GameScript 側は保持禁止
        const char* className = nullptr;
    };

    /// @brief GameScript が所有する Script object を Engine が参照するための不透明な ID
    using ScriptInstanceHandle = uint64_t;

    /// @brief DLL 自身が実装した ABI 世代を取得する entry point
    using ScriptGetAbiVersionFn = uint32_t(CUE_SCRIPT_CALL*)();

    /// @brief DLL が公開する Script 操作一覧を取得する entry point
    using ScriptGetExportsFn = ScriptResult(CUE_SCRIPT_CALL*)(struct ScriptModuleExports* a_outExports);

    /// @brief 指定 class が DLL に登録済みかを確認
    ///
    /// bool の ABI 差異を避けるため、真偽値は uint8_t で返却
    using ScriptHasClassFn = uint8_t(CUE_SCRIPT_CALL*)(const char* a_className);

    /// @brief Entity に対応する Script instance の生成を DLL 側へ要求
    using ScriptCreateInstanceFn = ScriptResult(CUE_SCRIPT_CALL*)(
        const ScriptInstanceCreateInfo* a_createInfo,
        ScriptInstanceHandle* a_outHandle);

    /// @brief Engine が不要になった Script instance を DLL 側で破棄
    using ScriptDestroyInstanceFn = ScriptResult(CUE_SCRIPT_CALL*)(ScriptInstanceHandle a_handle);

    /// @brief instance の初期化完了後に一度だけ呼び出すライフサイクル関数
    using ScriptOnCreateFn = ScriptResult(CUE_SCRIPT_CALL*)(ScriptInstanceHandle a_handle);

    /// @brief Play 中に Engine の更新周期で呼び出すライフサイクル関数
    using ScriptOnUpdateFn = ScriptResult(CUE_SCRIPT_CALL*)(ScriptInstanceHandle a_handle, float a_deltaTimeSeconds);

    /// @brief GameScript が Engine へ公開する操作一覧
    ///
    /// GameScript はこの構造体の末尾へのみ機能を追加し、既存の並びを変更禁止
    struct ScriptModuleExports final
    {
        /// @brief Engine が読み取れる構造体範囲
        ///
        /// ABI 拡張後に古い DLL を誤って読むことを防ぐため、Engine は必要なサイズを検証
        uint32_t structSize = 0u;

        /// @brief この操作一覧が従う ABI 世代
        uint32_t abiVersion = 0u;

        /// @brief class 登録を確認する関数
        ScriptHasClassFn hasClass = nullptr;

        /// @brief instance を生成する関数
        ScriptCreateInstanceFn createInstance = nullptr;

        /// @brief instance を破棄する関数
        ScriptDestroyInstanceFn destroyInstance = nullptr;

        /// @brief instance 作成直後の初期化関数
        ScriptOnCreateFn onCreate = nullptr;

        /// @brief instance を更新する関数
        ScriptOnUpdateFn onUpdate = nullptr;
    };
} // namespace Cue::Script
