#pragma once

// === C++ includes ===
#include <cstdint>

// Script ABI の基本方針
// - 文字列は UTF-8 を前提とする
// - DLL 境界では所有権を跨がせない
// - DLL 境界では POD と関数ポインタだけを扱う
// - 例外は DLL 境界を跨がせない
// - このヘッダは C++ から利用するが、関数境界は C ABI に固定する
    // - v1 ABI では Transform 操作、ログ出力、manual field 登録を公開する
// - ABI 拡張は末尾追加のみとし、既存フィールドの順序変更や削除は行わない
// - `structSize` は呼び出し側が見えている構造体サイズを示す
// - 受け手は `structSize` を見て、既知範囲だけを安全に読む

#if defined(_WIN32)
#if defined(CUE_SCRIPT_DLL_EXPORTS)
#define CUE_SCRIPT_EXPORT __declspec(dllexport)
#else
#define CUE_SCRIPT_EXPORT
#endif

#if defined(_MSC_VER)
#define CUE_SCRIPT_CALL __cdecl
#else
#define CUE_SCRIPT_CALL
#endif
#else
#define CUE_SCRIPT_EXPORT
#define CUE_SCRIPT_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    inline constexpr uint32_t k_cueScriptAbiVersion = 1u;
    inline constexpr uint64_t k_cueInvalidHandleValue = 0ull;

    using CueScriptAbiVersion = uint32_t;

    enum CueResult : uint32_t
    {
        CueResult_Ok = 0,
        CueResult_InvalidArgument = 1,
        CueResult_NotFound = 2,
        CueResult_Unsupported = 3,
        CueResult_InvalidState = 4,
        CueResult_InternalError = 5,
    };

    enum CueLogSeverity : uint32_t
    {
        CueLogSeverity_Info = 0,
        CueLogSeverity_Warning = 1,
        CueLogSeverity_Error = 2,
    };

    /// @brief DLL 境界で使う UTF-8 文字列ビューです。
    struct CueStringView
    {
        const char* data;
        uint32_t size;
    };

    /// @brief Entity を表す外部公開ハンドルです。
    /// `value == k_cueInvalidHandleValue` は無効値として扱います。
    struct CueEntityHandle
    {
        uint64_t value;
    };

    /// @brief Component を表す外部公開ハンドルです。
    /// v1 では予約のみで、実利用はまだ行いません。
    struct CueComponentHandle
    {
        uint64_t value;
    };

    /// @brief ScriptInstance を表す外部公開ハンドルです。
    /// `value == k_cueInvalidHandleValue` は無効値として扱います。
    struct CueScriptInstanceHandle
    {
        uint64_t value;
    };

    /// @brief 3 要素の float ベクトルです。
    struct CueFloat3
    {
        float x;
        float y;
        float z;
    };

    /// @brief Script 側へ公開する Transform データです。
    /// Engine 内部の TransformComponent をそのまま露出せず、
    /// DLL 境界ではこの POD だけを受け渡します。
    struct CueTransformData
    {
        CueFloat3 position;
        CueFloat3 rotation;
        CueFloat3 scale;
    };

    /// @brief Script public field の型です。
    enum CueScriptFieldType : uint32_t
    {
        CueScriptFieldType_Float = 0,
        CueScriptFieldType_Int32 = 1,
        CueScriptFieldType_Bool = 2,
    };

    /// @brief DLL 境界で渡す Script public field 値です。
    struct CueScriptFieldValue
    {
        CueStringView name;
        CueScriptFieldType type;
        float floatValue;
        int32_t intValue;
        uint8_t boolValue;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    /// @brief Script state の互換性判定に使う署名です。
    struct CueScriptStateDescriptor
    {
        uint32_t stateVersion;
        uint32_t stateSize;
        uint64_t schemaHash;
    };

    using CueLogFn = CueResult (CUE_SCRIPT_CALL*)(
        CueLogSeverity a_severity,
        CueStringView a_message
    );

    /// @brief EntityHandle が現在も有効なら 1、無効なら 0 を返します。
    using CueIsEntityValidFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);
    /// @brief Transform が存在するなら 1、存在しないなら 0 を返します。
    using CueHasTransformFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);

    /// @brief Transform の値を取得します。
    /// `a_outTransform` は呼び出し側が所有し、確保も解放も呼び出し側が行います。
    using CueGetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform
    );

    /// @brief Transform の値を設定します。
    /// `a_transform` が指すデータは呼び出し側所有で、呼び出し中だけ有効であれば十分です。
    using CueSetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform
    );

    /// @brief Script DLL が利用可能な Script クラス名を Engine へ通知します。
    /// `a_scriptClassName` は UTF-8 の非所有文字列です。
    using CueRegisterScriptClassFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName
    );

    /// @brief Script クラスに属する public field を Engine へ通知します。
    using CueRegisterScriptFieldFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue
    );

    /// @brief Engine から Script へ渡す関数テーブルです。
    /// v1 では末尾拡張のみを許可します。
    struct CueEngineApi
    {
        /// 呼び出し側がコンパイル時に見えている `CueEngineApi` のサイズです。
        uint32_t structSize;
        /// 利用する ABI version です。v1 は `k_cueScriptAbiVersion` を使います。
        CueScriptAbiVersion abiVersion;
        CueLogFn log;
        CueIsEntityValidFn isEntityValid;
        CueHasTransformFn hasTransform;
        CueGetTransformFn getTransform;
        CueSetTransformFn setTransform;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRegisterScriptClassFn registerScriptClass;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRegisterScriptFieldFn registerScriptField;
    };

    /// @brief Script インスタンス生成時の入力です。
    struct CueScriptCreateInfo
    {
        /// Script が紐付く Entity の外部公開ハンドルです。
        CueEntityHandle entityHandle;
        /// 生成対象 Script クラス名です。UTF-8、非所有です。
        CueStringView scriptName;
        /// Script public field 値配列です。null の場合は field なしとして扱います。
        const CueScriptFieldValue* fieldValues;
        /// `fieldValues` の要素数です。
        uint32_t fieldValueCount;
    };

    /// @brief Script DLL 側で利用可能な Script 種別を登録します。
    /// v1 では必須ではありませんが、将来の型レジストリ導入を見据えて残します。
    using CueRegisterScriptsFn =
        CueResult (CUE_SCRIPT_CALL*)(const CueEngineApi* a_engineApi);

    /// @brief ScriptInstance を生成します。
    /// 生成されたハンドルの実体所有権は Script DLL 側にあります。
    using CueCreateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueScriptCreateInfo* a_createInfo,
        CueScriptInstanceHandle* a_outInstanceHandle
    );

    /// @brief ScriptInstance を破棄します。
    /// 解放責務は生成した Script DLL 側にあります。
    using CueDestroyScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle
    );

    /// @brief ScriptInstance を 1 フレーム分更新します。
    using CueUpdateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        float a_deltaTimeSeconds
    );

    /// @brief ScriptInstance の state サイズを返します。
    using CueGetScriptInstanceStateSizeFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        uint32_t* a_outStateSize
    );

    /// @brief ScriptInstance の state を呼び出し側バッファへ serialize します。
    using CueSerializeScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize
    );

    /// @brief ScriptInstance の state を復元します。
    using CueRestoreScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize
    );

    /// @brief Script クラスの state 署名を返します。
    using CueGetScriptStateDescriptorFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        CueScriptStateDescriptor* a_outDescriptor
    );

    /// @brief Script DLL が Engine へ返す関数テーブルです。
    /// v1 では末尾拡張のみを許可します。
    struct CueScriptExports
    {
        /// 呼び出し側がコンパイル時に見えている `CueScriptExports` のサイズです。
        uint32_t structSize;
        /// 利用する ABI version です。Engine と一致しない場合はロード失敗とします。
        CueScriptAbiVersion abiVersion;
        CueRegisterScriptsFn registerScripts;
        CueCreateScriptInstanceFn createScriptInstance;
        CueDestroyScriptInstanceFn destroyScriptInstance;
        CueUpdateScriptInstanceFn updateScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptInstanceStateSizeFn getScriptInstanceStateSize;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSerializeScriptInstanceFn serializeScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRestoreScriptInstanceFn restoreScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptStateDescriptorFn getScriptStateDescriptor;
    };

    /// @brief Script DLL の ABI version を返します。
    /// この値は `k_cueScriptAbiVersion` と一致している必要があります。
    CUE_SCRIPT_EXPORT CueScriptAbiVersion CUE_SCRIPT_CALL
        cue_script_get_abi_version(void);

    /// @brief Script DLL の export table を返します。
    /// `a_outExports` は呼び出し側が確保し、呼び出し前に null でないことが必須です。
    CUE_SCRIPT_EXPORT CueResult CUE_SCRIPT_CALL cue_script_get_exports(
        CueScriptExports* a_outExports
    );

#ifdef __cplusplus
}
#endif
